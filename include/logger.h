#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

typedef enum {
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_DEBUG
} log_level_t;

int logger_init(const char *log_path);
void logger_close();

void log_msg(log_level_t level, const char *fmt, ...);

#endif
