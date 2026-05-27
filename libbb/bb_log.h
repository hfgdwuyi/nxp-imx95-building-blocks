#ifndef BB_LOG_H
#define BB_LOG_H

#include <stdarg.h>
#include <stddef.h>

typedef enum {
    BB_LOG_FATAL = 0,
    BB_LOG_ERROR = 1,
    BB_LOG_WARN  = 2,
    BB_LOG_INFO  = 3,
    BB_LOG_DEBUG = 4,
    BB_LOG_TRACE = 5,
} bb_log_level_t;

#define BB_LOG_TO_JOURNAL   (1 << 0)
#define BB_LOG_TO_FILE      (1 << 1)
#define BB_LOG_TO_STDERR    (1 << 2)
#define BB_LOG_TO_CRASH     (1 << 3)

int  bb_log_init(const char *ident, int dest_mask, const char *file_path, size_t max_size);
void bb_log_set_level(bb_log_level_t level);
bb_log_level_t bb_log_get_level(void);
void bb_log_write(bb_log_level_t level, const char *file, int line,
                  const char *fmt, ...) __attribute__((format(printf, 4, 5)));

#define bb_log_fatal(fmt, ...)  bb_log_write(BB_LOG_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define bb_log_error(fmt, ...)  bb_log_write(BB_LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define bb_log_warn(fmt, ...)   bb_log_write(BB_LOG_WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define bb_log_info(fmt, ...)   bb_log_write(BB_LOG_INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define bb_log_debug(fmt, ...)  bb_log_write(BB_LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define bb_log_trace(fmt, ...)  bb_log_write(BB_LOG_TRACE, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

void bb_log_crash(const char *ident, const char *reason, void *stack_ptr);
char *bb_log_diagnostics(const char *ident);
void bb_log_close(void);

#endif
