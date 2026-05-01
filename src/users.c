/**
 * tg-bot - Telegram bot for system administration
 * 
 * users.c - Active user session monitoring
 * 
 * Retrieves and formats information about currently logged-in users
 * using the utmp database. Provides both low-level access to raw
 * session data and high-level formatted output for Telegram messages.
 * 
 * Output format (Markdown):
 *   *👤 ACTIVE USERS: N*
 *   
 *   • 👤 `username`
 *     🖥 `tty`
 *     🌐 host
 *     ⏱ login_time
 * 
 * MIT License - Copyright (c) 2026 ironmist45
 *
 */

#include "users.h"
#include "logger.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <utmp.h>
#include <time.h>

// Maximum lengths for string buffers
#define MAX_LINE  256
#define MAX_FIELD 64

// ============================================================================
// INTERNAL UTILITY FUNCTIONS
// ============================================================================

/**
 * Format timestamp in thread-safe manner
 * 
 * @param t     Unix timestamp to format
 * @param buf   Output buffer
 * @param size  Size of output buffer
 */
static void format_time(time_t t, char *buf, size_t size) {
    struct tm tm_info;

    localtime_r(&t, &tm_info);
    strftime(buf, size, "%Y-%m-%d %H:%M", &tm_info);
}

/**
 * Copy utmp field into a fixed buffer with NULL/empty fallback.
 *
 * Different from the global safe_copy() (utils.c) in two ways:
 *   - argument order is (dst, src, size) — matches strncpy convention
 *     and is intentional for utmp field handling
 *   - substitutes "unknown" when src is NULL, which is appropriate
 *     for utmp fields that may be absent
 *
 * Named copy_utmp_field to avoid any confusion with the global
 * safe_copy(dst, size, src) from utils.c whose argument order differs.
 *
 * @param dst   Destination buffer (always null-terminated on return)
 * @param src   Source string (may be NULL)
 * @param size  Size of destination buffer
 */
static void copy_utmp_field(char *dst, const char *src, size_t size) {
    if (!src) {
        strncpy(dst, "unknown", size - 1);
    } else {
        strncpy(dst, src, size - 1);
    }
    dst[size - 1] = '\0';
}

/**
 * Safely append string with overflow protection
 * 
 * @param dst   Destination buffer (must be null-terminated)
 * @param size  Total size of destination buffer
 * @param src   Source string to append
 */
static void safe_append(char *dst, size_t size, const char *src) {
    size_t len = strlen(dst);

    if (len >= size - 1)
        return;

    size_t left = size - len - 1;
    strncat(dst, src, left);
}

// ============================================================================
// LOW-LEVEL SESSION ENUMERATION
// ============================================================================

/**
 * Retrieve raw list of active user sessions from utmp
 * 
 * @param buffer  Output buffer for formatted session list
 * @param size    Size of output buffer
 * @param req_id  16-bit request identifier for log correlation
 * @return        Number of active sessions, or -1 on error
 */
int users_get_logged(char *buffer, size_t size, unsigned short req_id) {

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

        format_time(entry->ut_tv.tv_sec, timebuf, sizeof(timebuf));

        char user[MAX_FIELD];
        char tty[MAX_FIELD];
        char host[MAX_FIELD];

        copy_utmp_field(user, entry->ut_user, sizeof(user));
        copy_utmp_field(tty,  entry->ut_line, sizeof(tty));

        // Host may be empty for local logins
        if (entry->ut_host && strlen(entry->ut_host) > 0)
            copy_utmp_field(host, entry->ut_host, sizeof(host));
        else
            copy_utmp_field(host, "local", sizeof(host));

        // Format session entry (Markdown)
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

        LOG_CMD(LOG_DEBUG,
            "users_get_logged: session user=%s tty=%s host=%s",
            user, tty, host);

        if (written < 0 || (size_t)written >= sizeof(line)) {
            LOG_CMD(LOG_WARN,
                "users_get_logged: line truncated (written=%d size=%zu)",
                written, sizeof(line));
        }

        // Check for buffer overflow before appending
        if (strlen(buffer) + strlen(line) >= size - 1) {
            LOG_CMD(LOG_WARN,
                "users_get_logged: buffer limit reached (buf=%zu line=%zu size=%zu)",
                strlen(buffer), strlen(line), size);

            safe_append(buffer, size, "...\n");
            break;
        }

        strcat(buffer, line);
        count++;
    }

    endutent();

    if (count == 0) {
        LOG_CMD(LOG_INFO, "req=%04x no active user sessions", req_id);
        safe_append(buffer, size, "No active sessions\n");
    }

    LOG_CMD(LOG_DEBUG, "users_get_logged(): count=%d", count);
    
    return count;
}

// ============================================================================
// HIGH-LEVEL FORMATTED OUTPUT
// ============================================================================

/**
 * Get formatted user list for Telegram response
 * 
 * Wraps users_get_logged() with a header containing total count.
 * 
 * @param buffer  Output buffer for complete response
 * @param size    Size of output buffer
 * @param req_id  16-bit request identifier for log correlation
 * @return        0 on success, -1 on error
 */
int users_get(char *buffer, size_t size, unsigned short req_id) {
    LOG_CMD(LOG_DEBUG, "req=%04x users_get()", req_id);
    
    char tmp[4096] = {0};

    int count = users_get_logged(tmp, sizeof(tmp), req_id);

    if (count < 0) {
        LOG_CMD(LOG_ERROR,
            "req=%04x users_get_logged failed (count=%d)",
            req_id, count);

        snprintf(buffer, size, "❌ Failed to get users");
        return -1;
    }

    int written = snprintf(buffer, size,
        "*👤 ACTIVE USERS: %d*\n\n%s",
        count,
        tmp
    );

    if (written < 0 || (size_t)written >= size) {
        LOG_CMD(LOG_WARN,
            "req=%04x users response truncated (written=%d size=%zu)",
            req_id, written, size);
    }

    LOG_CMD(LOG_INFO,
            "req=%04x users response: count=%d, bytes=%zu",
            req_id, count, strlen(buffer));

    return 0;
}
