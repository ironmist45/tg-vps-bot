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
 * MIT License
 * 
 * Copyright (c) 2026
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "users.h"
#include "logger.h"

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
 * Safely copy string with truncation protection
 * 
 * @param dst   Destination buffer
 * @param src   Source string (may be NULL)
 * @param size  Size of destination buffer
 */
static void safe_copy(char *dst, const char *src, size_t size) {
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
 * @return        Number of active sessions, or -1 on error
 */
int users_get_logged(char *buffer, size_t size) {

    LOG_CMD(LOG_DEBUG, "users_get_logged()");

    struct utmp *entry;

    buffer[0] = '\0';

    // Open utmp database for reading
    setutent();

    int count = 0;

    // Iterate through all utmp entries
    while ((entry = getutent()) != NULL) {

        // Only interested in user processes (active logins)
        if (entry->ut_type != USER_PROCESS)
            continue;

        char line[MAX_LINE];
        char timebuf[64];

        // Format login timestamp
        format_time(entry->ut_tv.tv_sec, timebuf, sizeof(timebuf));

        // Safely extract and truncate fields
        char user[MAX_FIELD];
        char tty[MAX_FIELD];
        char host[MAX_FIELD];

        safe_copy(user, entry->ut_user, sizeof(user));
        safe_copy(tty, entry->ut_line, sizeof(tty));

        // Host may be empty for local logins
        if (entry->ut_host && strlen(entry->ut_host) > 0)
            safe_copy(host, entry->ut_host, sizeof(host));
        else
            safe_copy(host, "local", sizeof(host));

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

    // Close utmp database
    endutent();

    // Handle case when no active sessions found
    if (count == 0) {
        LOG_CMD(LOG_INFO, "no active user sessions");
        safe_append(buffer, size, "_No active sessions_\n");
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
 * @return        0 on success, -1 on error
 */
int users_get(char *buffer, size_t size, unsigned short req_id) {
    LOG_CMD(LOG_DEBUG, "req=%04x users_get()", req_id);
    
    char tmp[4096] = {0};

    // Get raw session list
    int count = users_get_logged(tmp, sizeof(tmp));

    if (count < 0) {
        LOG_CMD(LOG_ERROR,
            "req=%04x users_get_logged failed (count=%d)",
            count);

        snprintf(buffer, size, "❌ Failed to get users");
        return -1;
    }

    // Format final response with header
    int written = snprintf(buffer, size,
        "*👤 ACTIVE USERS: %d*\n\n%s",
        count,
        tmp
    );

    if (written < 0 || (size_t)written >= size) {
        LOG_CMD(LOG_WARN,
            "req=%04x users response truncated (written=%d size=%zu)",
            written, size);
    }

    LOG_CMD(LOG_INFO,
            "req=%04x users response: count=%d, bytes=%zu",
            req_id, count, strlen(buffer));

    return 0;
}
