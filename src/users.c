#include "users.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <utmp.h>
#include <time.h>

#define MAX_LINE 512

// ===== формат времени =====

static void format_time(time_t t, char *buf, size_t size) {
    struct tm *tm_info = localtime(&t);
    strftime(buf, size, "%Y-%m-%d %H:%M", tm_info);
}

// ===== основной API =====

int users_get_logged(char *buffer, size_t size) {

    struct utmp *entry;

    buffer[0] = '\0';

    strncat(buffer, "=== LOGGED USERS ===\n\n", size - strlen(buffer) - 1);

    setutent(); // открыть utmp

    int count = 0;

    while ((entry = getutent()) != NULL) {

        if (entry->ut_type == USER_PROCESS) {

            char line[MAX_LINE];
            char timebuf[64];

            format_time(entry->ut_tv.tv_sec, timebuf, sizeof(timebuf));

            snprintf(line, sizeof(line),
                     "%-10s %-12s %s\n",
                     entry->ut_user,
                     entry->ut_line,
                     timebuf);

            if (strlen(buffer) + strlen(line) >= size - 1) {
                break;
            }

            strcat(buffer, line);
            count++;
        }
    }

    endutent(); // закрыть

    if (count == 0) {
        strncat(buffer, "No active sessions\n", size - strlen(buffer) - 1);
    }

    return 0;
}
