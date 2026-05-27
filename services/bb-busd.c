/*
 * bb-busd - Building Block Message Bus Daemon
 *
 * Listens on Unix domain socket /run/bb-bus.sock
 * Protocol: line-based PUB/SUB/UNSUB/PING over SOCK_STREAM
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <errno.h>

#define MAX_CLIENTS    64
#define MAX_SUBS       256
#define BUF_SIZE       8192
#define SOCKET_PATH    "/run/bb-bus.sock"

typedef struct {
    int  fd;
    char buf[BUF_SIZE];
    int  buf_len;
} client_t;

typedef struct {
    char   topic[128];
    int    client_ids[MAX_CLIENTS];
    int    count;
} sub_t;

static client_t clients[MAX_CLIENTS];
static sub_t    subs[MAX_SUBS];
static int      num_clients = 0;
static int      num_subs = 0;
static int      running = 1;

static void cleanup(void) {
    unlink(SOCKET_PATH);
}

static int find_sub(const char *topic) {
    for (int i = 0; i < num_subs; i++) {
        if (strcmp(subs[i].topic, topic) == 0) return i;
    }
    return -1;
}

static int topic_match(const char *pattern, const char *topic) {
    if (strcmp(pattern, topic) == 0) return 1;
    int plen = strlen(pattern);
    if (plen > 0 && pattern[plen - 1] == '#' && strncmp(pattern, topic, plen - 1) == 0) return 1;
    return 0;
}

static void publish(const char *topic, const char *payload, int sender_fd) {
    char msg[BUF_SIZE];
    int len = snprintf(msg, sizeof(msg), "PUB %s %s\n", topic, payload);
    if (len < 0 || len >= (int)sizeof(msg)) return;

    for (int i = 0; i < num_subs; i++) {
        if (topic_match(subs[i].topic, topic)) {
            for (int j = 0; j < subs[i].count; j++) {
                int cfd = subs[i].client_ids[j];
                if (cfd != sender_fd) {
                    send(cfd, msg, len, MSG_NOSIGNAL);
                }
            }
        }
    }
}

static void handle_line(int fd, const char *line) {
    char cmd[16], topic[256], payload[BUF_SIZE];

    if (strncmp(line, "PUB ", 4) == 0) {
        if (sscanf(line, "PUB %255s %8191[^\n]", topic, payload) >= 2) {
            const char *p = line + 4;
            while (*p && *p != ' ') p++;
            if (*p == ' ') p++;
            publish(topic, p, fd);
        }
    }
    else if (strncmp(line, "SUB ", 4) == 0 && sscanf(line, "SUB %255s", topic) == 1) {
        int idx = find_sub(topic);
        if (idx < 0 && num_subs < MAX_SUBS) {
            idx = num_subs++;
            strncpy(subs[idx].topic, topic, sizeof(subs[idx].topic) - 1);
            subs[idx].count = 0;
        }
        if (idx >= 0 && subs[idx].count < MAX_CLIENTS) {
            int found = 0;
            for (int j = 0; j < subs[idx].count; j++) {
                if (subs[idx].client_ids[j] == fd) { found = 1; break; }
            }
            if (!found) {
                subs[idx].client_ids[subs[idx].count++] = fd;
            }
        }
    }
    else if (strncmp(line, "UNSUB ", 6) == 0 && sscanf(line, "UNSUB %255s", topic) == 1) {
        int idx = find_sub(topic);
        if (idx >= 0) {
            for (int j = 0; j < subs[idx].count; j++) {
                if (subs[idx].client_ids[j] == fd) {
                    subs[idx].client_ids[j] = subs[idx].client_ids[--subs[idx].count];
                    break;
                }
            }
        }
    }
    else if (strcmp(line, "PING") == 0) {
        const char *pong = "PONG\n";
        send(fd, pong, strlen(pong), MSG_NOSIGNAL);
    }
}

static void remove_client(int epfd, int idx) {
    int fd = clients[idx].fd;

    for (int i = 0; i < num_subs; i++) {
        for (int j = 0; j < subs[i].count; j++) {
            if (subs[i].client_ids[j] == fd) {
                subs[i].client_ids[j] = subs[i].client_ids[--subs[i].count];
                j--;
            }
        }
    }

    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);

    if (idx < num_clients - 1) {
        memmove(&clients[idx], &clients[idx + 1],
                (num_clients - idx - 1) * sizeof(client_t));
    }
    num_clients--;
}

int main(void) {
    signal(SIGTERM, (void(*)(int))cleanup);
    signal(SIGINT,  (void(*)(int))cleanup);

    unlink(SOCKET_PATH);

    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(srv); return 1;
    }
    if (listen(srv, 32) < 0) { perror("listen"); close(srv); return 1; }

    printf("[bb-busd] Listening on %s\n", SOCKET_PATH);

    int epfd = epoll_create1(0);
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = srv };
    epoll_ctl(epfd, EPOLL_CTL_ADD, srv, &ev);

    struct epoll_event events[64];

    while (running) {
        int nfds = epoll_wait(epfd, events, 64, 500);
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == srv) {
                int cfd = accept(srv, NULL, NULL);
                if (cfd >= 0 && num_clients < MAX_CLIENTS) {
                    struct epoll_event cev = {
                        .events = EPOLLIN | EPOLLHUP | EPOLLERR,
                        .data.u32 = (uint32_t)num_clients
                    };
                    epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &cev);
                    clients[num_clients].fd = cfd;
                    clients[num_clients].buf_len = 0;
                    num_clients++;
                }
            }
            else {
                int cidx = events[i].data.u32;
                if (events[i].events & EPOLLIN) {
                    client_t *c = &clients[cidx];
                    int avail = (int)sizeof(c->buf) - c->buf_len - 1;
                    int nr = recv(c->fd, c->buf + c->buf_len, avail, 0);
                    if (nr <= 0) {
                        remove_client(epfd, cidx);
                        continue;
                    }
                    c->buf_len += nr;
                    c->buf[c->buf_len] = '\0';

                    char *start = c->buf;
                    char *nl;
                    while ((nl = strchr(start, '\n')) != NULL) {
                        *nl = '\0';
                        handle_line(c->fd, start);
                        start = nl + 1;
                    }
                    if (start > c->buf) {
                        c->buf_len = c->buf + c->buf_len - start;
                        memmove(c->buf, start, c->buf_len);
                        c->buf[c->buf_len] = '\0';
                    }
                }
                if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                    remove_client(epfd, cidx);
                }
            }
        }
    }

    cleanup();
    close(epfd);
    return 0;
}
