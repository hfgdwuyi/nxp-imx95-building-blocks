#include "bb_log.h"
#include "bb_json.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#include <execinfo.h>
#endif

static struct {
    char            ident[64];
    int             dest_mask;
    int             log_fd;
    size_t          max_size;
    size_t          current_size;
    bb_log_level_t  level;
    int             crash_count;
    pthread_mutex_t lock;
} g_log = {
    .ident       = "unknown",
    .dest_mask   = BB_LOG_TO_STDERR,
    .log_fd      = -1,
    .max_size    = 1024 * 1024,
    .level       = BB_LOG_INFO,
    .crash_count = 0,
};

#define CRASH_DIR "/persist/crash"

int bb_log_init(const char *ident, int dest_mask, const char *file_path, size_t max_size)
{
    pthread_mutex_init(&g_log.lock, NULL);

    strncpy(g_log.ident, ident, sizeof(g_log.ident) - 1);
    g_log.ident[sizeof(g_log.ident) - 1] = '\0';
    g_log.dest_mask = dest_mask;
    if (max_size > 0) g_log.max_size = max_size;

    if (dest_mask & BB_LOG_TO_FILE) {
        g_log.log_fd = open(file_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (g_log.log_fd < 0) {
            fprintf(stderr, "[%s] Failed to open log file %s: %s\n",
                    ident, file_path, strerror(errno));
            g_log.dest_mask &= ~BB_LOG_TO_FILE;
        } else {
            g_log.current_size = lseek(g_log.log_fd, 0, SEEK_END);
        }
    }

    return 0;
}

void bb_log_set_level(bb_log_level_t level) { g_log.level = level; }
bb_log_level_t bb_log_get_level(void)       { return g_log.level; }

void bb_log_write(bb_log_level_t level, const char *file, int line,
                  const char *fmt, ...)
{
    if (level > g_log.level) return;
    if (!(g_log.dest_mask & (BB_LOG_TO_FILE | BB_LOG_TO_STDERR | BB_LOG_TO_JOURNAL)))
        return;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);

    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%dT%H:%M:%S", &tm);

    static const char *level_str[] = {"FATAL", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};

    char msgbuf[4096];
    va_list ap;
    va_start(ap, fmt);
    int msg_len = vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
    va_end(ap);
    if (msg_len < 0) return;
    if ((size_t)msg_len >= sizeof(msgbuf)) msg_len = sizeof(msgbuf) - 1;

    char linebuf[4608];
    int len = snprintf(linebuf, sizeof(linebuf),
                       "%s.%03ld [%s] %s %s:%d %s\n",
                       timebuf, ts.tv_nsec / 1000000,
                       level_str[level], g_log.ident, file, line, msgbuf);
    if (len < 0) return;

    pthread_mutex_lock(&g_log.lock);

    if ((g_log.dest_mask & BB_LOG_TO_FILE) && g_log.log_fd >= 0) {
        write(g_log.log_fd, linebuf, len);
        g_log.current_size += len;
        if (g_log.current_size > g_log.max_size) {
            ftruncate(g_log.log_fd, g_log.max_size / 2);
            g_log.current_size = g_log.max_size / 2;
        }
    }

    if (g_log.dest_mask & BB_LOG_TO_STDERR) {
        write(STDERR_FILENO, linebuf, len);
    }

    pthread_mutex_unlock(&g_log.lock);

    if (level == BB_LOG_FATAL) {
        bb_log_crash(g_log.ident, msgbuf, __builtin_frame_address(0));
    }
}

void bb_log_crash(const char *ident, const char *reason, void *stack_ptr)
{
    (void)stack_ptr;

    mkdir(CRASH_DIR, 0755);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    char path[256];
    snprintf(path, sizeof(path), "%s/crash-%lld.log",
             CRASH_DIR, (long long)ts.tv_sec);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;

    dprintf(fd, "Block: %s\n", ident);
    dprintf(fd, "Time: %lld.%09ld\n", (long long)ts.tv_sec, ts.tv_nsec);
    dprintf(fd, "Reason: %s\n", reason);

#ifdef __linux__
    void *bt[32];
    int n = backtrace(bt, 32);
    dprintf(fd, "Backtrace (%d frames):\n", n);
    backtrace_symbols_fd(bt, n, fd);
#endif

    close(fd);

    char linkpath[256];
    snprintf(linkpath, sizeof(linkpath), "%s/latest", CRASH_DIR);
    unlink(linkpath);
    symlink(path, linkpath);
}

char *bb_log_diagnostics(const char *ident)
{
    (void)ident;
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"log_level\":\"%s\",\"log_size\":%zu,\"crash_count\":%d}",
             (g_log.level == BB_LOG_DEBUG) ? "debug" :
             (g_log.level == BB_LOG_INFO)  ? "info"  :
             (g_log.level == BB_LOG_WARN)  ? "warn"  :
             (g_log.level == BB_LOG_ERROR) ? "error" : "fatal",
             g_log.current_size,
             g_log.crash_count);
    return strdup(buf);
}

void bb_log_close(void)
{
    if (g_log.log_fd >= 0) close(g_log.log_fd);
    g_log.log_fd = -1;
    pthread_mutex_destroy(&g_log.lock);
}
