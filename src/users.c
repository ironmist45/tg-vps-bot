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

    if (len >= size - 1)
        return;

    size_t left = size - len - 1;
    strncat(dst, src, left);
}

// ===== LOW LEVEL =====

int users_get_logged(char *buffer, size_t size) {

    struct utmp *entry;

    buffer[0] = '\0';

    setutent();

    int count = 0;

    while ((entry = getutent()) != NULL) {

        if (entry->ut_type == USER_PROCESS) {

            char line[MAX_LINE];
            char timebuf[64];

            format_time(entry->ut_tv.tv_sec,
                        timebuf,
                        sizeof(timebuf));

            const char *user = entry->ut_user;
            const char *tty  = entry->ut_line;

            // IP / host может быть пустым
            const char *host = entry->ut_host;
            if (!host || strlen(host) == 0)
                host = "local";

            snprintf(line, sizeof(line),
                     "• `%s` (%s)\n"
                     "  🌐 %s\n"
                     "  ⏱ %s\n",
                     user,
                     tty,
                     host,
                     timebuf);

            if (strlen(buffer) + strlen(line) >= size - 1)
                break;

            strcat(buffer, line);
            count++;
        }
    }

    endutent();

    if (count == 0) {
        safe_append(buffer, size,
            "_No active sessions_\n");
    }

    return count;
}

// ===== HIGH LEVEL =====

int users_get(char *buffer, size_t size) {

    char tmp[2048] = {0};

    int count = users_get_logged(tmp, sizeof(tmp));

    if (count < 0) {
        snprintf(buffer, size, "❌ Failed to get users");
        return -1;
    }

    snprintf(buffer, size,
        "*👤 USERS (%d)*\n\n%s",
        count,
        tmp
    );

    return 0;
}
