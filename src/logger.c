#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <pthread.h>   // 🔥 mutex

static FILE *log_file = NULL;
static int log_to_stderr = 0;  // fallback
static log_level_t current_log_level = LOG_INFO;  // ✅ НОВОЕ

// 🔥 thread safety
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// ===== уровень → строка =====

const char *logger_level_to_string(log_level_t level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO ";
        case LOG_WARN:  return "WARN ";
        case LOG_ERROR: return "ERROR";
        default:        return "UNKWN";
    }
}

// ===== timestamp (thread-safe + ms) =====

static void get_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm tm_info;

    if (localtime_r(&now, &tm_info) == NULL) {
        snprintf(buf, size, "0000-00-00 00:00:00.000");
        return;
    }

    // базовое время
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tm_info);

    // 🔥 добавляем миллисекунды
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    size_t len = strlen(buf);
    if (len < size) {
        snprintf(buf + len, size - len, ".%03ld",
                 ts.tv_nsec / 1000000);
    }
}

// ===== init =====

int logger_init(const char *path) {

    // 🔥 защита от NULL
    if (!path) {
        log_to_stderr = 1;
        fprintf(stderr, "logger: NULL path, using stderr\n");
        return -1;
    }

    log_file = fopen(path, "a");

    if (!log_file) {
        // fallback на stderr
        log_to_stderr = 1;
        fprintf(stderr, "logger: failed to open %s, using stderr\n", path);
        return -1;
    }

    log_to_stderr = 0;

    // line buffered (лучше для логов)
    setvbuf(log_file, NULL, _IOLBF, 0);

    return 0;
}

// ===== set log level =====

void logger_set_level(log_level_t level) {
    current_log_level = level;
}

// ===== close =====

void logger_close() {
    if (log_file) {
        fflush(log_file); // 🔥 критично
        fclose(log_file);
        log_file = NULL;
    }

    // 🔥 оставляем fallback включённым
    log_to_stderr = 1;
}

// ===== основной лог =====

void log_msg(log_level_t level, const char *fmt, ...) {

    // 🔥 фильтрация по уровню
    if (level > current_log_level) {
        return;
    }

    // 🔥 защита от NULL fmt
    if (!fmt) return;

    FILE *file_out = log_file;
    FILE *console_out = stderr;

    if (!file_out && !log_to_stderr) {
        return;
    }

    char timestamp[40];
    get_timestamp(timestamp, sizeof(timestamp));

    char message[1024];

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    // защита от ошибок форматирования
    if (written < 0) {
        strncpy(message, "log formatting error", sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
    }
    // защита от обрезки
    else if ((size_t)written >= sizeof(message)) {
        size_t len = sizeof(message);
        if (len > 4) {
            message[len - 4] = '.';
            message[len - 3] = '.';
            message[len - 2] = '.';
            message[len - 1] = '\0';
        }
    }

    // 🔥 critical section
    pthread_mutex_lock(&log_mutex);

    // ===== запись в файл =====
    if (file_out) {
        if (fprintf(file_out, "[%s] [%s] %s\n",
                    timestamp,
                    logger_level_to_string(level),
                    message) < 0) {

            fprintf(stderr, "[LOGGER ERROR] write to file failed\n");
        }

        fflush(file_out);
    }

    // ===== fallback если файла нет =====
    if (!file_out && log_to_stderr) {
        fprintf(stderr, "[%s] [%s] %s\n",
                timestamp,
                logger_level_to_string(level),
                message);

        fflush(stderr);

        pthread_mutex_unlock(&log_mutex);
        return;
    }

    // ===== дублирование в консоль =====
    if (console_out) {
        fprintf(console_out, "[%s] [%s] %s\n",
                timestamp,
                logger_level_to_string(level),
                message);

        fflush(console_out);
    }

    pthread_mutex_unlock(&log_mutex);
}
