#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

static FILE *log_file = NULL;

// ===== уровень → строка =====

static const char *level_to_string(log_level_t level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO ";
        case LOG_WARN:  return "WARN ";
        case LOG_ERROR: return "ERROR";
        default:        return "UNKWN";
    }
}

// ===== timestamp =====

static void get_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

// ===== init =====

int logger_init(const char *path) {

    log_file = fopen(path, "a");

    if (!log_file) {
        return -1;
    }

    // отключаем буферизацию (важно для логов)
    setvbuf(log_file, NULL, _IOLBF, 0);

    return 0;
}

// ===== close =====

void logger_close() {
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
}

// ===== основной лог =====

void log_msg(log_level_t level, const char *fmt, ...) {

    if (!log_file) return;

    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));

    // форматируем сообщение
    char message[1024];

    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    // финальный вывод
    fprintf(log_file, "[%s] [%s] %s\n",
            timestamp,
            level_to_string(level),
            message);

    fflush(log_file);
}
