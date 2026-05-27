/*
 * bb-cli - Building Block CLI Tool for i.MX95
 *
 * Usage:
 *   bb-cli ping                     - Check bus status
 *   bb-cli pub <topic> <payload>    - Publish message
 *   bb-cli sub <topic>              - Subscribe and listen
 *   bb-cli led <cmd> [k v ...]      - Control LED block
 *   bb-cli system boottime          - Show boot time log
 *   bb-cli system update-log        - Show update history
 *   bb-cli log level <block> <lvl>  - Set log level
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "bb_json.h"
#include "bb_persist.h"

#define BUS_PATH "/run/bb-bus.sock"

static int bus_connect(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, BUS_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(fd); return -1;
    }
    return fd;
}

static int send_line(int fd, const char *line) {
    int len = strlen(line);
    char buf[8192];
    snprintf(buf, sizeof(buf), "%s\n", line);
    return send(fd, buf, strlen(buf), 0) > 0 ? 0 : -1;
}

static int cmd_ping(void) {
    int fd = bus_connect();
    if (fd < 0) return 1;

    send_line(fd, "PING");
    char resp[64];
    int n = recv(fd, resp, sizeof(resp) - 1, 0);
    close(fd);

    if (n > 0) {
        resp[n] = '\0';
        printf("Bus: %s", resp);
        return 0;
    }
    printf("Bus: no response\n");
    return 1;
}

static int cmd_pub(const char *topic, const char *payload) {
    int fd = bus_connect();
    if (fd < 0) return 1;

    char line[8192];
    snprintf(line, sizeof(line), "PUB %s %s", topic, payload);
    send_line(fd, line);
    close(fd);
    printf("Published to %s\n", topic);
    return 0;
}

static int cmd_sub(const char *topic) {
    int fd = bus_connect();
    if (fd < 0) return 1;

    char line[8192];
    snprintf(line, sizeof(line), "SUB %s", topic);
    send_line(fd, line);

    printf("Subscribed to %s. Listening... (Ctrl+C to stop)\n", topic);

    char buf[8192];
    int buf_len = 0;
    while (1) {
        int n = recv(fd, buf + buf_len, sizeof(buf) - 1 - buf_len, 0);
        if (n <= 0) break;
        buf_len += n;
        buf[buf_len] = '\0';

        char *start = buf;
        char *nl;
        while ((nl = strchr(start, '\n')) != NULL) {
            *nl = '\0';
            if (strncmp(start, "PUB ", 4) == 0) {
                char *t = start + 4;
                char *p = strchr(t, ' ');
                if (p) {
                    *p = '\0';
                    printf("[%s] %s\n", t, p + 1);
                } else {
                    printf("%s\n", start);
                }
            } else {
                printf("%s\n", start);
            }
            start = nl + 1;
        }
        if (start > buf) {
            buf_len = buf + buf_len - start;
            memmove(buf, start, buf_len);
            buf[buf_len] = '\0';
        }
    }
    close(fd);
    return 0;
}

static int cmd_led(int argc, char *argv[]) {
    if (argc < 1) {
        fprintf(stderr, "Usage: bb-cli led <cmd> [key val ...]\n");
        return 1;
    }

    char payload[4096];
    bb_json_writer_t w;
    bb_json_init(&w, payload, sizeof(payload));
    bb_json_start_object(&w);
    bb_json_add_string(&w, "cmd", argv[0]);
    for (int i = 1; i + 1 < argc; i += 2) {
        char *end;
        long v = strtol(argv[i + 1], &end, 10);
        if (*end == '\0') {
            bb_json_add_int(&w, argv[i], (int)v);
        } else {
            bb_json_add_string(&w, argv[i], argv[i + 1]);
        }
    }
    bb_json_end_object(&w);

    return cmd_pub("/dev/bb-led/cmd", payload);
}

static int cmd_system_boottime(void) {
    bb_boot_entry_t entries[32];
    int n = bb_persist_boot_log_read(entries, 32);
    printf("Boot history (%d entries):\n", n);
    for (int i = 0; i < n; i++) {
        printf("  %llu  slot=%c  %s  %ums\n",
               (unsigned long long)entries[i].timestamp,
               entries[i].slot,
               entries[i].success ? "OK" : "FAIL",
               entries[i].boot_time_ms);
    }
    return 0;
}

static int cmd_system_update_log(void) {
    bb_update_entry_t entries[32];
    int n = bb_persist_update_log_read(entries, 32);
    printf("Update history (%d entries):\n", n);
    for (int i = 0; i < n; i++) {
        printf("  %llu  %s -> %s  slot=%c  %s\n",
               (unsigned long long)entries[i].timestamp,
               entries[i].from_version, entries[i].to_version,
               entries[i].target_slot,
               entries[i].success ? "OK" : "FAIL");
    }
    return 0;
}

static int cmd_log_level(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: bb-cli log level <block> <level>\n");
        return 1;
    }
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"cmd\":\"set_log_level\",\"block\":\"%s\",\"level\":\"%s\"}",
             argv[0], argv[1]);
    return cmd_pub("/system/cmd", payload);
}

static void usage(void) {
    printf("bb-cli - Building Block CLI for i.MX95\n\n"
           "Usage:\n"
           "  bb-cli ping                  Check bus status\n"
           "  bb-cli pub <topic> <json>    Publish message\n"
           "  bb-cli sub <topic>           Subscribe to topic\n"
           "  bb-cli led <cmd> [k v ...]   Control LED block\n"
           "  bb-cli system boottime       Show boot history\n"
           "  bb-cli system update-log     Show update history\n"
           "  bb-cli log level <blk> <lvl> Set log level\n\n"
           "LED commands:\n"
           "  bb-cli led blink on_ms 200 off_ms 200 count 5\n"
           "  bb-cli led solid state on\n"
           "  bb-cli led heartbeat\n"
           "  bb-cli led status\n"
           "  bb-cli led stop\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) { usage(); return 0; }

    if (strcmp(argv[1], "ping") == 0) {
        return cmd_ping();
    }
    else if (strcmp(argv[1], "pub") == 0 && argc >= 4) {
        return cmd_pub(argv[2], argv[3]);
    }
    else if (strcmp(argv[1], "sub") == 0 && argc >= 3) {
        return cmd_sub(argv[2]);
    }
    else if (strcmp(argv[1], "led") == 0 && argc >= 3) {
        return cmd_led(argc - 2, argv + 2);
    }
    else if (strcmp(argv[1], "system") == 0 && argc >= 3) {
        if (strcmp(argv[2], "boottime") == 0)
            return cmd_system_boottime();
        if (strcmp(argv[2], "update-log") == 0)
            return cmd_system_update_log();
    }
    else if (strcmp(argv[1], "log") == 0 && argc >= 4) {
        if (strcmp(argv[2], "level") == 0)
            return cmd_log_level(argc - 3, argv + 3);
    }

    usage();
    return 0;
}
