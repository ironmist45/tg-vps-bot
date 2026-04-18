#include "users.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <utmp.h>
#include <time.h>

#define MAX_LINE 256
#define MAX_FIELD 64

// ===== формат времени (thread-safe) =====

static void format_time(time_t t, char *buf, size_t size) {
    struct tm tm_info;

    localtime_r(&t, &tm_info);
    strftime(buf, size, "%Y-%m-%d %H:%M", &tm_info);
}

// ===== безопасное копирование строки =====

static void safe_copy(char *dst, const char *src, size_t size) {
    if (!src) {
        strncpy(dst, "unknown", size - 1);
    } else {
        strncpy(dst, src, size - 1);
    }
    dst[size - 1] = '\0';
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

    LOG_CMD(LOG_DEBUG, "users_get_logged()");

    struct utmp *entry;

    buffer[0] = '\0';

    setutent();

    int count = 0;

    while ((entry = getutent()) != NULL) {

        if (entry->ut_type != USER_PROCESS)
            continue;

        char line[MAX_LINE];
        char timebuf[64];

        format_time(entry->ut_tv.tv_sec,
                    timebuf,
                    sizeof(timebuf));

        // безопасные поля (обрезаем)
        char user[MAX_FIELD];
        char tty[MAX_FIELD];
        char host[MAX_FIELD];

        safe_copy(user, entry->ut_user, sizeof(user));
        safe_copy(tty, entry->ut_line, sizeof(tty));

        if (entry->ut_host && strlen(entry->ut_host) > 0)
            safe_copy(host, entry->ut_host, sizeof(host));
        else
            safe_copy(host, "local", sizeof(host));

        // формируем строку
        int written = snprintf(line, sizeof(line),
            "• 👤 `%s`\n"
            "  🖥 `%s`\n"
            "  🌐 %s\n"
            "  ⏱ %s\n\n",
            user,
            tty,
            host,
            timebuf
        );

        log_msg(LOG_DEBUG,
                "user session: user=%s tty=%s host=%s",
                user, tty, host);

        if (written < 0 || (size_t)written >= sizeof(line)) {
            log_msg(LOG_WARN, "users: line truncated");
        }

        // проверка переполнения
        if (strlen(buffer) + strlen(line) >= size - 1) {
            log_msg(LOG_WARN, "users buffer limit reached");
            safe_append(buffer, size, "...\n");
            break;
        }

        strcat(buffer, line);
        count++;
    }

    endutent();

    if (count == 0) {
        LOG_CMD(LOG_INFO, "no active user sessions");
        safe_append(buffer, size,
            "_No active sessions_\n");
    }

    LOG_CMD(LOG_DEBUG, "users_get_logged(): count=%d", count);
    
    return count;
}

// ===== HIGH LEVEL =====

int users_get(char *buffer, size_t size) {

    LOG_CMD(LOG_DEBUG, "users_get()");
    
    char tmp[4096] = {0};

    int count = users_get_logged(tmp, sizeof(tmp));

    if (count < 0) {
        LOG_CMD(LOG_ERROR,
            "users_get_logged failed (count=%d)",
            count);

        snprintf(buffer, size, "❌ Failed to get users");
        return -1;
    }

    // финальный красивый вывод
    int written = snprintf(buffer, size,
        "*👤 ACTIVE USERS: %d*\n\n%s",
        count,
        tmp
    );

    if (written < 0 || (size_t)written >= size) {
        LOG_CMD(LOG_WARN,
            "users response truncated (written=%d size=%zu)",
            written, size);
    }

    LOG_CMD(LOG_INFO,
            "users response: count=%d, bytes=%zu",
            count, strlen(buffer));

    return 0;
}
