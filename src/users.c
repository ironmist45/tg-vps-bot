#include "users.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <utmp.h>
#include <time.h>

#define MAX_LINE 256

// ===== формат времени (thread-safe) =====

static void format_time(time_t t, char *buf, size_t size) {
    struct tm tm_info;

    localtime_r(&t, &tm_info);
    strftime(buf, size, "%Y-%m-%d %H:%M", &tm_info);
}

// ===== безопасное добавление строки =====

static void safe_append(char *dst, size_t size, const char *src) {
    size_t len = strlen(dst);
    size_t left = (len < size) ? (size - len - 1) : 0;

    if (left > 0) {
        strncat(dst, src, left);
    }
}

// ===== основной API =====

int users_get_logged(char *buffer, size_t size) {

    struct utmp *entry;

    buffer[0] = '\0';

    safe_append(buffer, size,
        "*👤 LOGGED USERS*\n\n");

    setutent(); // открыть utmp

    int count = 0;

    while ((entry = getutent()) != NULL) {

        if (entry->ut_type == USER_PROCESS) {

            char line[MAX_LINE];
            char timebuf[64];

            format_time(entry->ut_tv.tv_sec,
                        timebuf,
                        sizeof(timebuf));

            // 👉 красивый Telegram-стиль
            snprintf(line, sizeof(line),
                     "• `%s` (%s)\n"
                     "  ⏱ %s\n",
                     entry->ut_user,
                     entry->ut_line,
                     timebuf);

            if (strlen(buffer) + strlen(line) >= size - 1)
                break;

            strcat(buffer, line);
            count++;
        }
    }

    endutent(); // закрыть

    if (count == 0) {
        safe_append(buffer, size,
            "_No active sessions_\n");
    }

    return 0;
}
