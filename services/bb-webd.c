/*
 * bb-webd - Building Block Web Dashboard Daemon
 *
 * Lightweight HTTP/1.1 server serving a dashboard and REST API
 * on port 8080. Reads system info from /proc and /sys.
 * Optionally connects to bb-busd for real-time block state.
 *
 * Compile: gcc -std=c11 -Wall -Os -static -o bb-webd bb-webd.c -lpthread
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/sysinfo.h>
#include <dirent.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define HTTP_PORT        8080
#define MAX_CLIENTS      64
#define BUF_SIZE         16384
#define MAX_PATH         256
#define WWW_ROOT         "/opt/building-blocks/www"

// ---- MIME types ----
static const char *mime_by_ext(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css")  == 0) return "text/css; charset=utf-8";
    if (strcmp(ext, ".js")   == 0) return "application/javascript; charset=utf-8";
    if (strcmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (strcmp(ext, ".svg")  == 0) return "image/svg+xml";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".ico")  == 0) return "image/x-icon";
    return "text/plain; charset=utf-8";
}

// ---- HTTP response helpers ----
static void http_ok(int fd, const char *mime) {
    dprintf(fd, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nConnection: close\r\n", mime);
}

static void http_nocache(int fd, const char *mime) {
    dprintf(fd, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n"
            "Cache-Control: no-cache\r\nConnection: close\r\n", mime);
}

static void http_not_found(int fd) {
    const char *body = "{\"error\":\"not found\"}";
    dprintf(fd, "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n"
            "Content-Length: %zu\r\nConnection: close\r\n\r\n%s", strlen(body), body);
}

static void http_method_not_allowed(int fd) {
    const char *body = "{\"error\":\"method not allowed\"}";
    dprintf(fd, "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: application/json\r\n"
            "Content-Length: %zu\r\nConnection: close\r\n\r\n%s", strlen(body), body);
}

static void http_server_error(int fd, const char *msg) {
    dprintf(fd, "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n"
            "Content-Length: %zu\r\nConnection: close\r\n\r\n%s", strlen(msg), msg);
}

// ---- Read file with size limit ----
static char *read_file(const char *path, size_t *out_len, size_t max_size) {
    int f = open(path, O_RDONLY);
    if (f < 0) return NULL;
    off_t sz = lseek(f, 0, SEEK_END);
    if (sz < 0 || (size_t)sz > max_size) { close(f); return NULL; }
    lseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    if (!buf) { close(f); return NULL; }
    ssize_t n = read(f, buf, sz);
    close(f);
    if (n < 0) { free(buf); return NULL; }
    buf[n] = '\0';
    *out_len = n;
    return buf;
}

// ---- Read single line from /proc file ----
static int read_proc_line(const char *path, char *buf, size_t size) {
    int f = open(path, O_RDONLY);
    if (f < 0) return -1;
    ssize_t n = read(f, buf, size - 1);
    close(f);
    if (n <= 0) return -1;
    buf[n] = '\0';
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';
    return 0;
}

// ---- Minimal JSON builder (inline, no external deps) ----
typedef struct {
    char *buf;
    size_t cap;
    size_t pos;
    int first;
} jw_t;

static void jw_init(jw_t *j, char *buf, size_t cap) {
    j->buf = buf; j->cap = cap; j->pos = 0; j->first = 1;
    buf[0] = '\0';
}
static void jw_raw(jw_t *j, const char *s) {
    size_t len = strlen(s);
    if (j->pos + len < j->cap) { memcpy(j->buf + j->pos, s, len); j->pos += len; j->buf[j->pos] = '\0'; }
}
static void jw_obj_start(jw_t *j) { jw_raw(j, "{"); j->first = 1; }
static void jw_obj_end(jw_t *j)   { jw_raw(j, "}"); j->first = 0; }
static void jw_arr_start(jw_t *j) { jw_raw(j, "["); j->first = 1; }
static void jw_arr_end(jw_t *j)   { jw_raw(j, "]"); j->first = 0; }
static void jw_comma(jw_t *j)     { if (!j->first) jw_raw(j, ","); j->first = 0; }
static void jw_kv_str(jw_t *j, const char *k, const char *v) {
    jw_comma(j);
    jw_raw(j, "\""); jw_raw(j, k); jw_raw(j, "\":\""); jw_raw(j, v ? v : ""); jw_raw(j, "\"");
}
static void jw_kv_int(jw_t *j, const char *k, long long v) {
    jw_comma(j);
    jw_raw(j, "\""); jw_raw(j, k); jw_raw(j, "\":");
    char num[32]; snprintf(num, sizeof(num), "%lld", v);
    jw_raw(j, num);
}
static void jw_kv_double(jw_t *j, const char *k, double v) {
    jw_comma(j);
    jw_raw(j, "\""); jw_raw(j, k); jw_raw(j, "\":");
    char num[32]; snprintf(num, sizeof(num), "%.2f", v);
    jw_raw(j, num);
}

// ---- JSON string escape ----
static void json_escape_str(jw_t *j, const char *s) {
    jw_raw(j, "\"");
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"':  jw_raw(j, "\\\""); break;
            case '\\': jw_raw(j, "\\\\"); break;
            case '\n': jw_raw(j, "\\n"); break;
            case '\r': jw_raw(j, "\\r"); break;
            case '\t': jw_raw(j, "\\t"); break;
            default:
                if (j->pos + 1 < j->cap) { j->buf[j->pos++] = *p; j->buf[j->pos] = '\0'; }
        }
    }
    jw_raw(j, "\"");
}

// ---- API Handlers ----

// GET /api/system - system overview
static void handle_api_system(int fd) {
    char json[4096];
    jw_t j;
    jw_init(&j, json, sizeof(json));
    jw_obj_start(&j);

    // Uptime
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        jw_kv_int(&j, "uptime_seconds", si.uptime);
        long days = si.uptime / 86400, hrs = (si.uptime % 86400) / 3600,
             min = (si.uptime % 3600) / 60, sec = si.uptime % 60;
        char up[64]; snprintf(up, sizeof(up), "%ldd %ldh %ldm %lds", days, hrs, min, sec);
        jw_kv_str(&j, "uptime", up);
    }

    // Load average
    double load[3];
    if (getloadavg(load, 3) == 3) {
        jw_kv_double(&j, "load_1m", load[0]);
        jw_kv_double(&j, "load_5m", load[1]);
        jw_kv_double(&j, "load_15m", load[2]);
    }

    // Memory
    if (sysinfo(&si) == 0) {
        jw_kv_int(&j, "mem_total_kb", (si.totalram      * si.mem_unit) / 1024);
        jw_kv_int(&j, "mem_free_kb",  (si.freeram       * si.mem_unit) / 1024);
        jw_kv_int(&j, "mem_used_kb",  ((si.totalram - si.freeram) * si.mem_unit) / 1024);
        jw_kv_int(&j, "swap_total_kb", (si.totalswap     * si.mem_unit) / 1024);
        jw_kv_int(&j, "swap_free_kb",  (si.freeswap      * si.mem_unit) / 1024);
    }

    // CPU count
    jw_kv_int(&j, "cpu_count", sysconf(_SC_NPROCESSORS_CONF));

    // Kernel version
    char buf[256];
    if (read_proc_line("/proc/version", buf, sizeof(buf)) == 0)
        jw_kv_str(&j, "kernel", buf);

    // Hostname
    if (gethostname(buf, sizeof(buf)) == 0)
        jw_kv_str(&j, "hostname", buf);

    // Product
    jw_kv_str(&j, "product", "NXP i.MX95 EVK");

    // Disk usage on / (rootfs)
    struct statfs sfs;
    if (statfs("/", &sfs) == 0) {
        jw_kv_int(&j, "disk_total_kb", ((unsigned long long)sfs.f_blocks * sfs.f_bsize) / 1024);
        jw_kv_int(&j, "disk_free_kb",  ((unsigned long long)sfs.f_bfree  * sfs.f_bsize) / 1024);
        jw_kv_int(&j, "disk_used_kb",  ((unsigned long long)(sfs.f_blocks - sfs.f_bfree) * sfs.f_bsize) / 1024);
    }
    // /persist
    if (statfs("/persist", &sfs) == 0) {
        jw_kv_int(&j, "persist_total_kb", ((unsigned long long)sfs.f_blocks * sfs.f_bsize) / 1024);
        jw_kv_int(&j, "persist_free_kb",  ((unsigned long long)sfs.f_bfree  * sfs.f_bsize) / 1024);
    }
    // /log
    if (statfs("/log", &sfs) == 0) {
        jw_kv_int(&j, "log_total_kb", ((unsigned long long)sfs.f_blocks * sfs.f_bsize) / 1024);
        jw_kv_int(&j, "log_free_kb",  ((unsigned long long)sfs.f_bfree  * sfs.f_bsize) / 1024);
    }

    // Temperature (thermal zone 0)
    char temp[32];
    if (read_proc_line("/sys/class/thermal/thermal_zone0/temp", temp, sizeof(temp)) == 0) {
        long t = atol(temp);
        jw_kv_int(&j, "cpu_temp_mc", t);
        jw_kv_double(&j, "cpu_temp_c", t / 1000.0);
    }

    jw_obj_end(&j);

    http_nocache(fd, "application/json; charset=utf-8");
    dprintf(fd, "Content-Length: %zu\r\n\r\n%s", strlen(json), json);
}

// GET /api/cpu - per-core CPU stats
static void handle_api_cpu(int fd) {
    char json[4096];
    jw_t j;
    jw_init(&j, json, sizeof(json));
    jw_arr_start(&j);

    char buf[2048];
    int f = open("/proc/stat", O_RDONLY);
    if (f >= 0) {
        ssize_t n = read(f, buf, sizeof(buf) - 1);
        close(f);
        if (n > 0) {
            buf[n] = '\0';
            char *line, *save;
            for (line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
                char name[16];
                unsigned long long user, nice, sys, idle, iowait, irq, softirq;
                if (sscanf(line, "%15s %llu %llu %llu %llu %llu %llu %llu",
                           name, &user, &nice, &sys, &idle, &iowait, &irq, &softirq) >= 4) {
                    if (strncmp(name, "cpu", 3) == 0 && name[3] >= '0' && name[3] <= '9') {
                        jw_comma(&j);
                        jw_obj_start(&j);
                        jw_kv_str(&j, "core", name);
                        jw_kv_int(&j, "user",    (long long)user);
                        jw_kv_int(&j, "system",  (long long)(sys + irq + softirq));
                        jw_kv_int(&j, "idle",    (long long)(idle + iowait));
                        unsigned long long total = user + nice + sys + idle + iowait + irq + softirq;
                        jw_kv_int(&j, "total",   (long long)total);
                        jw_obj_end(&j);
                    }
                }
            }
        }
    }

    jw_arr_end(&j);
    http_nocache(fd, "application/json; charset=utf-8");
    dprintf(fd, "Content-Length: %zu\r\n\r\n%s", strlen(json), json);
}

// GET /api/partitions - partition info
static void handle_api_partitions(int fd) {
    char json[2048];
    jw_t j;
    jw_init(&j, json, sizeof(json));
    jw_obj_start(&j);

    struct statfs sfs;
    struct { const char *mp; const char *label; } mounts[] = {
        {"/",         "rootfs"},
        {"/boot/a",   "boot-a"},
        {"/persist",  "persist"},
        {"/log",      "log"},
        {NULL, NULL}
    };
    for (int i = 0; mounts[i].mp; i++) {
        if (statfs(mounts[i].mp, &sfs) == 0) {
            jw_obj_start(&j);
            jw_kv_str(&j, "label",    mounts[i].label);
            jw_kv_str(&j, "mount",    mounts[i].mp);
            jw_kv_int(&j, "total_kb", (long long)((unsigned long long)sfs.f_blocks * sfs.f_bsize) / 1024);
            jw_kv_int(&j, "free_kb",  (long long)((unsigned long long)sfs.f_bfree * sfs.f_bsize) / 1024);
            jw_kv_int(&j, "used_kb",  (long long)((unsigned long long)(sfs.f_blocks - sfs.f_bfree) * sfs.f_bsize) / 1024);
            jw_obj_end(&j);
        }
    }

    jw_obj_end(&j);
    http_nocache(fd, "application/json; charset=utf-8");
    dprintf(fd, "Content-Length: %zu\r\n\r\n%s", strlen(json), json);
}

// GET /api/network - network interfaces
static void handle_api_network(int fd) {
    char json[4096];
    jw_t j;
    jw_init(&j, json, sizeof(json));
    jw_arr_start(&j);

    // Read /proc/net/dev for interface stats
    char buf[4096];
    int f = open("/proc/net/dev", O_RDONLY);
    if (f >= 0) {
        ssize_t n = read(f, buf, sizeof(buf) - 1);
        close(f);
        if (n > 0) {
            buf[n] = '\0';
            char *save;
            for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
                char iface[32];
                unsigned long long rx_b, rx_p, tx_b, tx_p;
                if (sscanf(line, " %31[^:]: %llu %llu %*u %*u %*u %*u %*u %*u %llu %llu",
                           iface, &rx_b, &rx_p, &tx_b, &tx_p) >= 3) {
                    if (strcmp(iface, "lo") == 0) continue;
                    jw_comma(&j);
                    jw_obj_start(&j);
                    jw_kv_str(&j, "name", iface);
                    jw_kv_int(&j, "rx_bytes",  (long long)rx_b);
                    jw_kv_int(&j, "rx_packets",(long long)rx_p);
                    jw_kv_int(&j, "tx_bytes",  (long long)tx_b);
                    jw_kv_int(&j, "tx_packets",(long long)tx_p);
                    // Try to get IP from /sys
                    char addr_path[128], ip[64];
                    snprintf(addr_path, sizeof(addr_path),
                             "/sys/class/net/%s/address", iface);
                    if (read_proc_line(addr_path, ip, sizeof(ip)) == 0)
                        jw_kv_str(&j, "mac", ip);
                    jw_obj_end(&j);
                }
            }
        }
    }

    jw_arr_end(&j);
    http_nocache(fd, "application/json; charset=utf-8");
    dprintf(fd, "Content-Length: %zu\r\n\r\n%s", strlen(json), json);
}

// GET /api/processes - top processes
static void handle_api_processes(int fd) {
    char json[4096];
    jw_t j;
    jw_init(&j, json, sizeof(json));
    jw_arr_start(&j);

    // Simple: just list building-block processes from /proc
    // In production, use popen("ps") - but we avoid fork/exec for size
    // Read /proc/*/comm and filter
    DIR *proc = opendir("/proc");
    if (proc) {
        struct dirent *de;
        while ((de = readdir(proc))) {
            if (de->d_name[0] < '0' || de->d_name[0] > '9') continue;
            char comm_path[128], comm[64];
            snprintf(comm_path, sizeof(comm_path), "/proc/%s/comm", de->d_name);
            int cf = open(comm_path, O_RDONLY);
            if (cf < 0) continue;
            ssize_t cn = read(cf, comm, sizeof(comm) - 1);
            close(cf);
            if (cn <= 0) continue;
            comm[cn] = '\0';
            char *nl = strchr(comm, '\n'); if (nl) *nl = '\0';

            if (strncmp(comm, "bb-", 3) == 0) {
                jw_comma(&j);
                jw_obj_start(&j);
                jw_kv_str(&j, "name", comm);
                jw_kv_int(&j, "pid", atoi(de->d_name));
                // Read RSS from /proc/*/statm
                snprintf(comm_path, sizeof(comm_path), "/proc/%s/statm", de->d_name);
                int sf = open(comm_path, O_RDONLY);
                if (sf >= 0) {
                    char sbuf[128];
                    ssize_t sn = read(sf, sbuf, sizeof(sbuf) - 1);
                    close(sf);
                    if (sn > 0) {
                        sbuf[sn] = '\0';
                        unsigned long size, rss;
                        if (sscanf(sbuf, "%lu %lu", &size, &rss) == 2)
                            jw_kv_int(&j, "rss_kb", (long long)(rss * 4)); // pagesize=4K
                    }
                }
                jw_obj_end(&j);
            }
        }
        closedir(proc);
    }

    jw_arr_end(&j);
    http_nocache(fd, "application/json; charset=utf-8");
    dprintf(fd, "Content-Length: %zu\r\n\r\n%s", strlen(json), json);
}

// GET /api/logs - recent system log
static void handle_api_logs(int fd) {
    char json[8192];
    jw_t j;
    jw_init(&j, json, sizeof(json));
    // Try reading from journal or log partition
    jw_arr_start(&j);

    char buf[2048];
    int f = open("/var/log/messages", O_RDONLY);
    if (f < 0) f = open("/log/messages", O_RDONLY);
    if (f >= 0) {
        lseek(f, (off_t)(-(off_t)sizeof(buf)), SEEK_END);
        ssize_t n = read(f, buf, sizeof(buf) - 1);
        close(f);
        if (n > 0) {
            buf[n] = '\0';
            char *save, *line = strtok_r(buf, "\n", &save);
            while (line && j.pos < sizeof(json) - 256) {
                jw_comma(&j);
                json_escape_str(&j, line);
                line = strtok_r(NULL, "\n", &save);
            }
        }
    }

    jw_arr_end(&j);
    http_nocache(fd, "application/json; charset=utf-8");
    dprintf(fd, "Content-Length: %zu\r\n\r\n%s", strlen(json), json);
}

// GET /api/health - health status
static void handle_api_health(int fd) {
    char json[2048];
    jw_t j;
    jw_init(&j, json, sizeof(json));
    jw_obj_start(&j);
    jw_kv_str(&j, "status", "ok");

    // Check bb-busd is running
    struct stat st;
    jw_kv_str(&j, "busd", (stat("/run/bb-bus.sock", &st) == 0) ? "running" : "stopped");

    // Check watchdog
    jw_kv_str(&j, "watchdog",
              (access("/dev/watchdog0", W_OK) == 0) ? "available" : "unavailable");

    // Check persist partition
    jw_kv_str(&j, "persist",
              (access("/persist", R_OK) == 0) ? "mounted" : "unmounted");

    // Check boot slot from cmdline
    char cmdline[512];
    if (read_proc_line("/proc/cmdline", cmdline, sizeof(cmdline)) == 0) {
        jw_kv_str(&j, "boot_slot",
                  strstr(cmdline, "root=/dev/mmcblk0p6") ? "a" :
                  strstr(cmdline, "root=/dev/mmcblk0p7") ? "b" : "unknown");
    }

    // Firmware version
    char ver[64];
    if (read_proc_line("/opt/building-blocks/etc/version", ver, sizeof(ver)) == 0)
        jw_kv_str(&j, "version", ver);
    else
        jw_kv_str(&j, "version", "unknown");

    jw_obj_end(&j);
    http_nocache(fd, "application/json; charset=utf-8");
    dprintf(fd, "Content-Length: %zu\r\n\r\n%s", strlen(json), json);
}

// ---- Embedded dashboard HTML ----
static const char DASHBOARD_HTML[] =
    "<!DOCTYPE html>\n"
    "<html lang=\"en\">\n"
    "<head>\n"
    "<meta charset=\"UTF-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
    "<title>i.MX95 Building Blocks</title>\n"
    "<style>\n"
    "*{box-sizing:border-box;margin:0;padding:0}\n"
    "body{font:14px/1.5 system-ui,sans-serif;background:#0f172a;color:#e2e8f0}\n"
    ".header{background:#1e293b;padding:16px 24px;border-bottom:2px solid #334155;display:flex;align-items:center;justify-content:space-between}\n"
    ".header h1{font-size:20px;font-weight:600;color:#38bdf8}\n"
    ".header .status{font-size:12px;padding:4px 12px;border-radius:12px;background:#065f46;color:#6ee7b7}\n"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:16px;padding:16px;max-width:1400px;margin:0 auto}\n"
    ".card{background:#1e293b;border-radius:8px;padding:16px;border:1px solid #334155}\n"
    ".card h2{font-size:14px;font-weight:600;color:#94a3b8;text-transform:uppercase;letter-spacing:.05em;margin-bottom:12px}\n"
    ".kv{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid #1e293b}\n"
    ".kv:last-child{border-bottom:none}\n"
    ".kv .k{color:#94a3b8}\n"
    ".kv .v{color:#e2e8f0;font-family:monospace}\n"
    ".bar{height:6px;border-radius:3px;background:#334155;margin:8px 0;overflow:hidden}\n"
    ".bar .fill{height:100%;border-radius:3px;transition:width .5s}\n"
    ".fill-green{background:#10b981}\n"
    ".fill-yellow{background:#f59e0b}\n"
    ".fill-red{background:#ef4444}\n"
    ".fill-blue{background:#3b82f6}\n"
    ".proc-row{display:flex;justify-content:space-between;padding:4px 0;font-size:13px}\n"
    ".proc-row .name{color:#e2e8f0}\n"
    ".proc-row .mem{color:#94a3b8;font-family:monospace}\n"
    ".footer{text-align:center;padding:16px;color:#475569;font-size:12px}\n"
    ".error{color:#fca5a5;font-size:12px}\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "<div class=\"header\">\n"
    "<h1>i.MX95 Building Blocks</h1>\n"
    "<span class=\"status\" id=\"conn-status\">connecting</span>\n"
    "</div>\n"
    "<div class=\"grid\">\n"
    "<div class=\"card\"><h2>System</h2><div id=\"system-info\">loading...</div></div>\n"
    "<div class=\"card\"><h2>Memory</h2><div id=\"mem-info\">loading...</div></div>\n"
    "<div class=\"card\"><h2>CPU</h2><div id=\"cpu-info\">loading...</div></div>\n"
    "<div class=\"card\"><h2>Disk</h2><div id=\"disk-info\">loading...</div></div>\n"
    "<div class=\"card\"><h2>Network</h2><div id=\"net-info\">loading...</div></div>\n"
    "<div class=\"card\"><h2>Processes</h2><div id=\"proc-info\">loading...</div></div>\n"
    "<div class=\"card\"><h2>Health</h2><div id=\"health-info\">loading...</div></div>\n"
    "</div>\n"
    "<div class=\"footer\">i.MX95 Building Blocks &mdash; Refresh: <span id=\"refresh-timer\">0</span>s ago</div>\n"
    "<script>\n"
    "var REFRESH=5000;\n"
    "var lastRefresh=0;\n"
    "function kv(k,v){return '<div class=\"kv\"><span class=\"k\">'+k+'</span><span class=\"v\">'+v+'</span></div>';}\n"
    "function pctBar(pct,cls){"
    "  var c=cls||(pct>90?'fill-red':pct>70?'fill-yellow':'fill-green');"
    "  return '<div class=\"bar\"><div class=\"fill '+c+'\" style=\"width:'+Math.min(100,pct)+'%\"></div></div>';}\n"
    "async function fetchJSON(url){try{var r=await fetch(url);if(!r.ok)throw Error(r.status);return await r.json();}catch(e){return null;}}\n"
    "async function refresh(){\n"
    "  var data=await fetchJSON('/api/system');\n"
    "  if(data){\n"
    "    document.getElementById('conn-status').textContent='online';"
    "    document.getElementById('conn-status').style.background='#065f46';"
    "    document.getElementById('conn-status').style.color='#6ee7b7';"
    "    var si='';"
    "    si+=kv('Hostname',data.hostname);"
    "    si+=kv('Kernel',data.kernel||'-');"
    "    si+=kv('Uptime',data.uptime);"
    "    si+=kv('Load',data.load_1m+' / '+data.load_5m+' / '+data.load_15m);"
    "    si+=kv('CPU Temp',(data.cpu_temp_c||0)+'C');"
    "    document.getElementById('system-info').innerHTML=si;"
    "    var memUsedPct=(data.mem_total_kb?((data.mem_used_kb/data.mem_total_kb)*100):0);"
    "    var mi='';"
    "    mi+=kv('Total',(data.mem_total_kb/1024).toFixed(0)+' MB');"
    "    mi+=kv('Used',(data.mem_used_kb/1024).toFixed(0)+' MB');"
    "    mi+=kv('Free',(data.mem_free_kb/1024).toFixed(0)+' MB');"
    "    mi+=pctBar(memUsedPct)+'<span style=font-size:11px;color:#94a3b8>'+memUsedPct.toFixed(1)+'% used</span>';"
    "    if(data.swap_total_kb>0){mi+=kv('Swap',((data.swap_total_kb-data.swap_free_kb)/1024).toFixed(0)+' / '+(data.swap_total_kb/1024).toFixed(0)+' MB');}"
    "    document.getElementById('mem-info').innerHTML=mi;"
    "    var di='';"
    "    di+=kv('Rootfs',(data.disk_used_kb/1024).toFixed(0)+' / '+(data.disk_total_kb/1024).toFixed(0)+' MB');"
    "    di+=pctBar(data.disk_total_kb?((data.disk_used_kb/data.disk_total_kb)*100):0);"
    "    if(data.persist_total_kb>0){di+=kv('Persist',(data.persist_free_kb/1024).toFixed(0)+' / '+(data.persist_total_kb/1024).toFixed(0)+' MB free');}"
    "    if(data.log_total_kb>0){di+=kv('Log',(data.log_free_kb/1024).toFixed(0)+' / '+(data.log_total_kb/1024).toFixed(0)+' MB free');}"
    "    document.getElementById('disk-info').innerHTML=di;"
    "    var hi='';"
    "    hi+=kv('Status',data.status||'?');"
    "    hi+=kv('Bus Daemon',data.busd||'?');"
    "    hi+=kv('Watchdog',data.watchdog||'?');"
    "    hi+=kv('Persist',data.persist||'?');"
    "    hi+=kv('Boot Slot',data.boot_slot||'?');"
    "    hi+=kv('Version',data.version||'?');"
    "    document.getElementById('health-info').innerHTML=hi;"
    "  }\n"
    "  var cpu=await fetchJSON('/api/cpu');\n"
    "  if(cpu && cpu.length){\n"
    "    var ci='';\n"
    "    for(var i=0;i<cpu.length;i++){"
    "      var c=cpu[i];var total=c.total||1;var used=c.total-c.idle;var upct=(used/total)*100;"
    "      ci+=kv(c.core,upct.toFixed(0)+'%');"
    "      ci+=pctBar(upct,'fill-blue');"
    "    }"
    "    document.getElementById('cpu-info').innerHTML=ci;"
    "  }\n"
    "  var net=await fetchJSON('/api/network');\n"
    "  if(net && net.length){\n"
    "    var ni='';\n"
    "    for(var i=0;i<net.length;i++){"
    "      var n=net[i];"
    "      ni+='<div style=\"margin-bottom:8px\">';"
    "      ni+='<div style=\"font-weight:600;color:#38bdf8\">'+n.name+'</div>';"
    "      if(n.mac)ni+=kv('MAC',n.mac);"
    "      ni+=kv('RX',(n.rx_bytes/1024).toFixed(0)+' KB / '+n.rx_packets+' pkts');"
    "      ni+=kv('TX',(n.tx_bytes/1024).toFixed(0)+' KB / '+n.tx_packets+' pkts');"
    "      ni+='</div>';"
    "    }"
    "    document.getElementById('net-info').innerHTML=ni;"
    "  }\n"
    "  var proc=await fetchJSON('/api/processes');\n"
    "  if(proc && proc.length){\n"
    "    var pi='';\n"
    "    for(var i=0;i<proc.length;i++){"
    "      var p=proc[i];"
    "      pi+='<div class=\"proc-row\"><span class=\"name\">'+p.name+'</span><span class=\"mem\">PID '+p.pid+' &middot; '+(p.rss_kb||0)+' KB</span></div>';"
    "    }"
    "    document.getElementById('proc-info').innerHTML=pi||'<span class=error>none</span>';"
    "  }\n"
    "  lastRefresh=0;\n"
    "}\n"
    "setInterval(function(){lastRefresh++;document.getElementById('refresh-timer').textContent=lastRefresh*3;refresh();},3000);\n"
    "refresh();\n"
    "</script>\n"
    "</body>\n"
    "</html>\n";

// ---- Route dispatch ----
typedef struct {
    const char *method;
    const char *path;
    void (*handler)(int fd);
} route_t;

static const route_t routes[] = {
    {"GET", "/api/system",     handle_api_system},
    {"GET", "/api/cpu",        handle_api_cpu},
    {"GET", "/api/partitions", handle_api_partitions},
    {"GET", "/api/network",    handle_api_network},
    {"GET", "/api/processes",  handle_api_processes},
    {"GET", "/api/logs",       handle_api_logs},
    {"GET", "/api/health",     handle_api_health},
    {NULL, NULL, NULL}
};

static void serve_dashboard(int fd) {
    http_ok(fd, "text/html; charset=utf-8");
    dprintf(fd, "Content-Length: %zu\r\n\r\n%s", strlen(DASHBOARD_HTML), DASHBOARD_HTML);
}

static void serve_static_file(int fd, const char *path) {
    // Path traversal guard
    if (strstr(path, "..") || strstr(path, "//")) {
        http_not_found(fd);
        return;
    }

    char fpath[MAX_PATH];
    snprintf(fpath, sizeof(fpath), "%s%s", WWW_ROOT, path);

    // If no www dir or file not found, fall back to embedded
    size_t len;
    char *content = read_file(fpath, &len, 1024 * 1024);
    if (!content) {
        // Try index.html as fallback
        if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) {
            serve_dashboard(fd);
            return;
        }
        http_not_found(fd);
        return;
    }

    http_ok(fd, mime_by_ext(path));
    dprintf(fd, "Content-Length: %zu\r\n\r\n", len);
    write(fd, content, len);
    free(content);
}

// ---- HTTP request parser ----
typedef struct {
    char method[16];
    char path[MAX_PATH];
    int  headers_done;
} http_req_t;

static int parse_http(const char *raw, int len, http_req_t *req) {
    memset(req, 0, sizeof(*req));
    const char *end = raw + len;
    const char *nl = memmem(raw, len, "\r\n", 2);
    if (!nl) return -1;

    // Parse request line: GET /path HTTP/1.x
    if (sscanf(raw, "%15s %255s", req->method, req->path) < 2) return -1;
    if (req->path[0] != '/') return -1;

    // Find end of headers
    const char *body = memmem(nl + 2, end - (nl + 2), "\r\n\r\n", 4);
    req->headers_done = body ? (int)(body + 4 - raw) : len;
    return 0;
}

static void handle_request(int fd, const char *data, int len) {
    http_req_t req;
    if (parse_http(data, len, &req) < 0) {
        http_server_error(fd, "Bad request");
        return;
    }

    if (strcmp(req.method, "GET") != 0) {
        http_method_not_allowed(fd);
        return;
    }

    // Route matching
    for (const route_t *r = routes; r->method; r++) {
        if (strcmp(req.path, r->path) == 0) {
            r->handler(fd);
            return;
        }
    }

    // Static file or dashboard
    if (strcmp(req.path, "/") == 0 || strcmp(req.path, "/index.html") == 0) {
        serve_dashboard(fd);
    } else {
        serve_static_file(fd, req.path);
    }
}

// ---- Client state ----
typedef struct {
    int  fd;
    char buf[BUF_SIZE];
    int  buf_len;
    int  header_len;
} client_t;

static client_t clients[MAX_CLIENTS];
static int num_clients = 0;
static int running = 1;

static void remove_client(int epfd, int idx) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, clients[idx].fd, NULL);
    close(clients[idx].fd);
    if (idx < num_clients - 1) {
        memmove(&clients[idx], &clients[idx + 1],
                (num_clients - idx - 1) * sizeof(client_t));
    }
    num_clients--;
}

// ---- Main ----
int main(int argc, char **argv) {
    int port = HTTP_PORT;
    if (argc > 1) port = atoi(argv[1]);

    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, (void(*)(int))exit);
    signal(SIGINT,  (void(*)(int))exit);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(srv, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(srv); return 1;
    }
    if (listen(srv, 32) < 0) { perror("listen"); close(srv); return 1; }

    printf("[bb-webd] Listening on http://0.0.0.0:%d\n", port);

    int epfd = epoll_create1(0);
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = srv };
    epoll_ctl(epfd, EPOLL_CTL_ADD, srv, &ev);

    struct epoll_event events[64];

    while (running) {
        int nfds = epoll_wait(epfd, events, 64, 1000);
        for (int i = 0; i < nfds; i++) {
            int efd = events[i].data.fd;
            if (efd == srv) {
                int cfd = accept(srv, NULL, NULL);
                if (cfd >= 0 && num_clients < MAX_CLIENTS) {
                    struct epoll_event cev = {
                        .events = EPOLLIN | EPOLLHUP | EPOLLERR,
                        .data.u32 = (uint32_t)num_clients
                    };
                    epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &cev);
                    clients[num_clients].fd = cfd;
                    clients[num_clients].buf_len = 0;
                    clients[num_clients].header_len = 0;
                    num_clients++;
                } else if (cfd >= 0) {
                    close(cfd);
                }
            } else {
                int cidx = events[i].data.u32;
                if (cidx >= num_clients) continue;

                if (events[i].events & EPOLLIN) {
                    client_t *c = &clients[cidx];
                    int avail = (int)sizeof(c->buf) - c->buf_len - 1;
                    if (avail <= 0) { remove_client(epfd, cidx); continue; }
                    int nr = recv(c->fd, c->buf + c->buf_len, avail, 0);
                    if (nr <= 0) { remove_client(epfd, cidx); continue; }
                    c->buf_len += nr;
                    c->buf[c->buf_len] = '\0';

                    // Check if headers are complete
                    char *body = strstr(c->buf, "\r\n\r\n");
                    if (body) {
                        int hdr_end = (int)(body - c->buf) + 4;
                        handle_request(c->fd, c->buf, hdr_end);
                        // HTTP/1.0 style: close after response
                        remove_client(epfd, cidx);
                    } else if (c->buf_len > 8192) {
                        // Too large, bail
                        remove_client(epfd, cidx);
                    }
                }
                if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                    remove_client(epfd, cidx);
                }
            }
        }
    }

    close(epfd);
    close(srv);
    return 0;
}
