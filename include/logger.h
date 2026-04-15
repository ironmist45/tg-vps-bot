#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

typedef enum {
    LOG_ERROR = 0,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG
} log_level_t;

int logger_init(const char *path);
void logger_close();

void logger_set_level(log_level_t level);

void log_msg(log_level_t level, const char *fmt, ...);

// ✅ НОВОЕ
const char *logger_level_to_string(log_level_t level);

#endif
