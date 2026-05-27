#include "bb_bus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <errno.h>

int bb_bus_connect(bb_bus_t *bus, const char *socket_path) {
    memset(bus, 0, sizeof(*bus));
    strncpy(bus->socket_path, socket_path, sizeof(bus->socket_path) - 1);

    bus->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (bus->fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(bus->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(bus->fd);
        bus->fd = -1;
        return -1;
    }
    return 0;
}

static int send_line(bb_bus_t *bus, const char *line) {
    if (bus->fd < 0) return -1;
    size_t len = strlen(line);
    char *msg = alloca(len + 2);
    memcpy(msg, line, len);
    msg[len] = '\n';
    msg[len + 1] = '\0';
    return send(bus->fd, msg, len + 1, MSG_NOSIGNAL) > 0 ? 0 : -1;
}

int bb_bus_publish(bb_bus_t *bus, const char *topic, const char *payload) {
    char line[BB_MAX_LINE];
    snprintf(line, sizeof(line), "PUB %s %s", topic, payload);
    return send_line(bus, line);
}

int bb_bus_subscribe(bb_bus_t *bus, const char *topic) {
    char line[BB_MAX_LINE];
    snprintf(line, sizeof(line), "SUB %s", topic);
    return send_line(bus, line);
}

int bb_bus_unsubscribe(bb_bus_t *bus, const char *topic) {
    char line[BB_MAX_LINE];
    snprintf(line, sizeof(line), "UNSUB %s", topic);
    return send_line(bus, line);
}

int bb_bus_ping(bb_bus_t *bus) {
    if (send_line(bus, "PING") < 0) return -1;
    char buf[64];
    int n = recv(bus->fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        return strncmp(buf, "PONG", 4) == 0 ? 0 : -1;
    }
    return -1;
}

void bb_bus_set_handler(bb_bus_t *bus, bb_msg_handler_t handler, void *userdata) {
    bus->handler = handler;
    bus->userdata = userdata;
}

int bb_bus_poll(bb_bus_t *bus, int timeout_ms) {
    if (bus->fd < 0) return -1;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(bus->fd, &fds);

    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    int ret = select(bus->fd + 1, &fds, NULL, NULL, timeout_ms >= 0 ? &tv : NULL);
    if (ret <= 0) return ret;

    static char buf[BB_MAX_LINE];
    static int buf_pos = 0;

    int n = recv(bus->fd, buf + buf_pos, sizeof(buf) - 1 - buf_pos, 0);
    if (n <= 0) return -1;
    buf_pos += n;
    buf[buf_pos] = '\0';

    // Process complete lines
    char *line_start = buf;
    char *nl;
    while ((nl = strchr(line_start, '\n')) != NULL) {
        *nl = '\0';
        if (strncmp(line_start, "PUB ", 4) == 0) {
            char *topic = line_start + 4;
            char *payload = strchr(topic, ' ');
            if (payload) {
                *payload = '\0';
                payload++;
                if (bus->handler) {
                    bus->handler(topic, payload, bus->userdata);
                }
            }
        }
        line_start = nl + 1;
    }

    if (line_start > buf) {
        buf_pos = buf + buf_pos - line_start;
        memmove(buf, line_start, buf_pos);
        buf[buf_pos] = '\0';
    }
    return 0;
}

void bb_bus_close(bb_bus_t *bus) {
    if (bus->fd >= 0) {
        close(bus->fd);
        bus->fd = -1;
    }
}
