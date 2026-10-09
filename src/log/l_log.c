#include "o_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define LINE_CAPACITY 1024

static struct {
    OLogLevel min_level;
    OLogSinks sinks;
    FILE *file;
} g_log = {O_LOG_INFO, O_LOG_SINK_STDOUT | O_LOG_SINK_STDERR, NULL};

static const char *const k_level_names[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};

void log_set_level(OLogLevel min_level) {
    g_log.min_level = min_level;
}

void log_set_sinks(OLogSinks sinks) {
    g_log.sinks = sinks;
}

bool log_open_file(const char *path) {
    log_close_file();
    g_log.file = path ? fopen(path, "wb") : NULL;
    if (!g_log.file) return false;
    g_log.sinks |= O_LOG_SINK_FILE;
    return true;
}

void log_close_file(void) {
    if (g_log.file) fclose(g_log.file);
    g_log.file = NULL;
    g_log.sinks &= ~(OLogSinks)O_LOG_SINK_FILE;
}

static const char *basename_of(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    return base;
}

static FILE *console_for(OLogLevel level) {
    const bool to_stdout = g_log.sinks & O_LOG_SINK_STDOUT;
    const bool to_stderr = g_log.sinks & O_LOG_SINK_STDERR;
    if (to_stdout && to_stderr) return level >= O_LOG_WARN ? stderr : stdout;
    if (to_stderr) return stderr;
    return to_stdout ? stdout : NULL;
}

static void write_line(FILE *sink, const char *line, size_t len, bool flush) {
    if (!sink) return;
    fwrite(line, 1, len, sink);
    if (flush) fflush(sink);
}

void log_write(OLogLevel level, const char *file, int line, const char *fmt, ...) {
    if (level < g_log.min_level) return;

    char buffer[LINE_CAPACITY];
    const size_t max_len = sizeof buffer - 1;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    int hour = t ? t->tm_hour : 0, min = t ? t->tm_min : 0, sec = t ? t->tm_sec : 0;

    int n = snprintf(buffer, sizeof buffer, "%02d:%02d:%02d %s %s:%d  ", hour, min, sec, k_level_names[level],
                     basename_of(file), line);
    if (n < 0) n = 0;
    size_t len = (size_t)n > max_len ? max_len : (size_t)n;

    va_list args;
    va_start(args, fmt);
    int m = vsnprintf(buffer + len, sizeof buffer - len, fmt, args);
    va_end(args);
    if (m > 0) len += (size_t)m;

    if (len > max_len) {
        len = max_len;
        memcpy(buffer + len - 3, "...", 3);
    }
    buffer[len++] = '\n';

    const bool urgent = level >= O_LOG_WARN;
    write_line(console_for(level), buffer, len, urgent);
    if (g_log.sinks & O_LOG_SINK_FILE) write_line(g_log.file, buffer, len, urgent);
}
