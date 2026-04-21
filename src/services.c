/**
 * tg-bot - Telegram bot for system administration
 * 
 * services.c - Systemd service status monitoring
 * 
 * Provides the /services command for checking the status of
 * whitelisted systemd services.
 * 
 * Features:
 *   - Whitelist-based access control (only predefined services)
 *   - Service name validation (prevents injection)
 *   - Status detection: active, inactive, failed, activating
 *   - Emoji indicators: 🟢 UP, 🔴 DOWN, 🟡 FAIL/STARTING, ⚪ UNKNOWN
 *   - Formatted output in Markdown code block
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

#include "exec.h"
#include "services.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

// ============================================================================
// SERVICE WHITELIST
// ============================================================================

typedef struct {
    const char *name;     // Systemd unit name
    const char *display;  // User-friendly display name
} service_t;

/**
 * Whitelist of allowed services.
 * Only services listed here can be queried.
 */
static service_t services[] = {
    {"ssh",               "SSH"},
    {"shadowsocks-libev", "Shadowsocks"},
    {"mtg",               "MTG"},
};

static const int services_count = sizeof(services) / sizeof(services[0]);

// ============================================================================
// SERVICE NAME VALIDATION
// ============================================================================

/**
 * Validate service name format (prevent command injection)
 * 
 * Allowed characters: alphanumeric, hyphen, underscore.
 * 
 * @param s  Service name to validate
 * @return   1 if valid, 0 otherwise
 */
static int is_valid_service_name(const char *s) {
    if (!s || *s == '\0') return 0;

    for (size_t i = 0; s[i]; i++) {
        char c = s[i];

        if (!(isalnum((unsigned char)c) ||
              c == '-' || c == '_')) {
            return 0;
        }
    }

    return 1;
}

// ============================================================================
// SERVICE STATUS QUERY
// ============================================================================

/**
 * Get current status of a single service via systemctl
 * 
 * Executes: sudo systemctl is-active <service>
 * 
 * @param service  Systemd unit name (must be validated)
 * @param out      Output buffer for status string
 * @param size     Size of output buffer
 * @return         0 on success, -1 on error
 */
static int get_service_status(const char *service, char *out, size_t size) {

    // Validate service name before passing to shell
    if (!is_valid_service_name(service)) {
        LOG_SYS(LOG_ERROR, "Invalid service name: %s", service);
        if (size > 0) {
            snprintf(out, size, "invalid");
        }
        return -1;
    }

    char *const args[] = {
        "sudo",
        "-n",
        "systemctl",
        "is-active",
        (char *)service,
        NULL
    };

    exec_result_t res;
    int rc = exec_command(args, out, size, NULL, &res);

    if (rc != 0) {
        if (res.status == EXEC_TIMEOUT) {
            LOG_SYS(LOG_ERROR, "service %s: timeout", service);
        } else {
            LOG_SYS(LOG_ERROR,
                "service %s: exec failed (%s)",
                service,
                exec_status_str(res.status));
        }

        if (size > 0) {
            snprintf(out, size, "unknown");
        }
        return -1;
    }

    if (res.exit_code != 0) {
        LOG_SYS(LOG_DEBUG,
            "service %s: non-zero exit (%d)",
            service,
            res.exit_code);
    }

    // Remove trailing newline from systemctl output
    out[strcspn(out, "\n")] = 0;

    // Handle empty output
    if (out[0] == '\0') {
        snprintf(out, size, "unknown");
    }

    // Trim leading spaces (defensive)
    char *p = out;
    while (*p == ' ') p++;

    if (p != out) {
        memmove(out, p, strlen(p) + 1);
    }

    LOG_SYS(LOG_DEBUG, "service=%s status=%s", service, out);

    return 0;
}

// ============================================================================
// STATUS FORMATTING
// ============================================================================

/**
 * Convert systemctl status to user-friendly emoji indicator
 * 
 * @param status  Raw status string from systemctl
 * @return        Formatted status with emoji
 */
static const char *format_status(const char *status) {

    if (strcmp(status, "active") == 0)
        return "🟢 UP";

    if (strcmp(status, "inactive") == 0)
        return "🔴 DOWN";

    if (strcmp(status, "failed") == 0)
        return "🟡 FAIL";

    if (strcmp(status, "activating") == 0)
        return "🟡 STARTING";

    return "⚪ UNKNOWN";
}

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Get formatted status of all whitelisted services
 * 
 * Output format (Markdown):
 *   *🧩 SERVICES*
 *   
 *   ```
 *   SSH             : 🟢 UP
 *   Shadowsocks     : 🔴 DOWN
 *   MTG             : 🟢 UP
 *   ```
 * 
 * @param buffer  Output buffer for formatted response
 * @param size    Size of output buffer
 * @param req_id  16-bit request identifier for log correlation
 * @return        0 on success, -1 on error
 */
int services_get_status(char *buffer, size_t size, unsigned short req_id) {

    LOG_CMD(LOG_INFO, "req=%04x services_get_status()", req_id);
    LOG_STATE(LOG_DEBUG, "req=%04x services count: %d", req_id, services_count);

    if (!buffer || size == 0) {
        LOG_SYS(LOG_ERROR, "req=%04x invalid buffer", req_id);
        return -1;
    }

    buffer[0] = '\0';
    size_t offset = 0;

    // ------------------------------------------------------------------------
    // Format header
    // ------------------------------------------------------------------------
    int written = snprintf(buffer + offset,
                           size - offset,
                           "*🧩 SERVICES*\n\n```\n");

    if (written < 0) {
        LOG_SYS(LOG_ERROR, "snprintf error (header)");
        return -1;
    }

    if ((size_t)written >= size - offset) {
        LOG_SYS(LOG_WARN, "services buffer truncated (header)");
        return 0;
    }

    offset += (size_t)written;

    // ------------------------------------------------------------------------
    // Query and format each service
    // ------------------------------------------------------------------------
    for (int i = 0; i < services_count; i++) {
        char status[64] = {0};

        if (get_service_status(services[i].name,
                               status,
                               sizeof(status)) != 0) {
            LOG_SYS(LOG_WARN,
                    "Failed to get status: %s",
                    services[i].name);
        }

        const char *status_fmt = format_status(status);

        LOG_SYS(LOG_DEBUG,
                "service=%s display=%s status=%s",
                services[i].name,
                services[i].display,
                status_fmt);

        int written = snprintf(buffer + offset,
                               size - offset,
                               "%-16s : %s\n",
                               services[i].display,
                               status_fmt);

        if (written < 0) {
            LOG_SYS(LOG_ERROR, "snprintf error (line)");
            break;
        }

        if ((size_t)written >= size - offset) {
            LOG_CMD(LOG_WARN,
                "services buffer limit reached (offset=%zu size=%zu)",
                offset, size);
            break;
        }

        offset += (size_t)written;
    }

    // ------------------------------------------------------------------------
    // Format footer (close code block)
    // ------------------------------------------------------------------------
    int written_end = snprintf(buffer + offset,
                               size - offset,
                               "\n```");

    if (written_end < 0) {
        LOG_SYS(LOG_ERROR, "snprintf error (footer)");
    } else if ((size_t)written_end >= size - offset) {
        LOG_SYS(LOG_WARN, "services buffer truncated (footer)");
    } else {
        offset += (size_t)written_end;
    }
    
        LOG_STATE(LOG_DEBUG, "req=%04x services response ready: %zu bytes", req_id, offset);

    return 0;
}

    return 0;
}
