#include "logger.h"
#include <time.h>
#include <stdarg.h>
#include <string.h>

static FILE *log_file = NULL;

static const char *level_str[] = {
    "INFO",
    "WARN",
    "ERROR",
    "DEBUG"
};

int logger_init(const char *log_path) {
    log_file = fopen(log_path, "a");
    if (!log_file) {
        return -1;
    }
    return 0;
}

void logger_close() {
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
}

static void get_time_str(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", t);
}

void log_msg(log_level_t level, const char *fmt, ...) {
    char time_buf[32];
    get_time_str(time_buf, sizeof(time_buf));

    char message[512];

    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    // ==== Консоль (олдскульно) ====
    printf("[%s] %-5s | %s\n", time_buf, level_str[level], message);

    // ==== Файл ====
    if (log_file) {
        fprintf(log_file, "[%s] %-5s | %s\n",
                time_buf, level_str[level], message);
        fflush(log_file);
    }
}
