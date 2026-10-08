#ifndef O_LOG_H
#define O_LOG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum OLogLevel {
    O_LOG_DEBUG,
    O_LOG_INFO,
    O_LOG_WARN,
    O_LOG_ERROR,
} OLogLevel;

typedef uint32_t OLogSinks;
enum {
    O_LOG_SINK_STDOUT = 1u << 0,
    O_LOG_SINK_STDERR = 1u << 1,
    O_LOG_SINK_FILE = 1u << 2,
};

void log_set_level(OLogLevel min_level);
// With both STDOUT and STDERR enabled, WARN and ERROR go to stderr only.
void log_set_sinks(OLogSinks sinks);
bool log_open_file(const char *path);
void log_close_file(void);

#if defined(__GNUC__) || defined(__clang__)
#define O_LOG_PRINTF(fmt_index, args_index) __attribute__((format(printf, fmt_index, args_index)))
#else
#define O_LOG_PRINTF(fmt_index, args_index)
#endif

void log_write(OLogLevel level, const char *file, int line, const char *fmt, ...) O_LOG_PRINTF(4, 5);

#define LOG_DEBUG(...) log_write(O_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) log_write(O_LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) log_write(O_LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_write(O_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif
