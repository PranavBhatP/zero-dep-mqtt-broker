#include "logger.h"
#include <time.h>
#include <string.h>
#include <stdint.h>

static log_level_t g_current_level = LOG_LEVEL_INFO;

void logger_set_level(log_level_t level) {
    g_current_level = level;
}

log_level_t logger_get_level(void) {
    return g_current_level;
}

static void log_print(log_level_t level, const char *color, const char *tag, const char *fmt, va_list args) {
    if (level < g_current_level) return;

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);

    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_info);

    /* Print timestamp and colored level tag */
    fprintf(stderr, "[%s] %s[%s]\033[0m ", time_str, color, tag);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    fflush(stderr);
}

void log_debug(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_print(LOG_LEVEL_DEBUG, "\033[36m", "DEBUG", fmt, args); /* Cyan */
    va_end(args);
}

void log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_print(LOG_LEVEL_INFO, "\033[32m", "INFO ", fmt, args); /* Green */
    va_end(args);
}

void log_warn(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_print(LOG_LEVEL_WARN, "\033[33m", "WARN ", fmt, args); /* Yellow */
    va_end(args);
}

void log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_print(LOG_LEVEL_ERROR, "\033[31m", "ERROR", fmt, args); /* Red */
    va_end(args);
}

void log_hex_dump(const char *prefix, const void *data, size_t len) {
    if (g_current_level > LOG_LEVEL_DEBUG || !data || len == 0) return;

    const uint8_t *bytes = (const uint8_t *)data;
    fprintf(stderr, "[HEX DUMP] %s (%zu bytes):\n  ", prefix, len);
    for (size_t i = 0; i < len; i++) {
        fprintf(stderr, "%02X ", bytes[i]);
        if ((i + 1) % 16 == 0 && (i + 1) < len) {
            fprintf(stderr, "\n  ");
        }
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}
