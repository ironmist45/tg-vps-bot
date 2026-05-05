/**
 * tg-bot - Telegram bot for system administration
 * 
 * logs.c - Systemd journal log retrieval and filtering
 * 
 * Provides the /logs command for viewing systemd journal logs.
 * Features:
 *   - Service alias mapping via shared services_config (e.g., "ssh" → "ssh")
 *   - Configurable line count (default 30, max LOGS_MAX_LINES)
 *   - Case-insensitive multi-keyword filtering
 *   - Automatic fallback to global journal if service has no logs
 *   - ANSI escape code stripping
 *   - Reboot marker detection
 *   - Output truncation protection
 *
 * Service list is defined in services_config.c — edit that file to
 * add, remove, or rename monitored services.
 * 
 * MIT License
 * 
 * Copyright (c) 2026 ironmist45
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

#define _GNU_SOURCE

#include "logs.h"
#include "logger.h"
#include "exec.h"
#include "utils.h"
#include "logs_filter.h"
#include "services_config.h"
#include "config.h"

#include <strings.h>   /* strcasestr */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* Maximum line length for processing */
#define MAX_LINE    256

/* Number of lines to log at DEBUG level for troubleshooting */
#define DEBUG_LINES 5

/*
 * Maximum number of lines per /logs request.
 * Requests exceeding this value are clamped silently.
 */
#define LOGS_MAX_LINES       200

/*
 * Soft buffer cap: stop appending log lines when the response buffer
 * exceeds this size to stay well within Telegram's 4096-char limit.
 */
#define LOGS_BUFFER_SOFT_CAP 3500

/* Max args for journalctl command array */
#define JOURNALCTL_ARGS_MAX  16

// ============================================================================
// INTERNAL HELPER FUNCTIONS
// ============================================================================

/**
 * Check if string consists entirely of digits
 */
static int is_number(const char *s) {
    if (!s || *s == '\0') return 0;

    for (int i = 0; s[i]; i++) {
        if (!isdigit((unsigned char)s[i]))
            return 0;
    }
    return 1;
}

/**
 * Resolve user-facing service alias to systemd unit name.
 *
 * Looks up alias in the shared g_services table (services_config.c).
 *
 * @param alias  User-provided service name (e.g. "shadowsocks")
 * @return       Systemd unit name (e.g. "shadowsocks-libev"), or NULL if not allowed
 */
static const char *resolve_service(const char *alias) {
    for (int i = 0; i < g_services_count; i++) {
        if (strcmp(alias, g_services[i].alias) == 0)
            return g_services[i].unit;
    }
    return NULL;
}

/**
 * Safely append string with overflow protection.
 * Used for short menu output in logs_get() where position tracking
 * is not needed.
 */
static void safe_append(char *dst, size_t size, const char *src) {
    size_t len  = strlen(dst);
    size_t left = (len < size) ? (size - len - 1) : 0;

    if (left > 0)
        strncat(dst, src, left);
}

/**
 * Append string at known position — O(1), avoids repeated strlen().
 *
 * @param dst   Output buffer
 * @param size  Total buffer size
 * @param pos   Current write position (offset from dst)
 * @param src   String to append
 * @return      New write position after append
 */
static size_t append_at(char *dst, size_t size, size_t pos, const char *src)
{
    if (pos >= size - 1) return pos;

    size_t left = size - pos - 1;
    size_t slen = strlen(src);
    size_t copy = slen < left ? slen : left;

    memcpy(dst + pos, src, copy);
    dst[pos + copy] = '\0';

    return pos + copy;
}

/**
 * Remove carriage return from line (Windows line ending cleanup)
 */
static void sanitize_line(char *line) {
    line[strcspn(line, "\r")] = '\0';
}

/**
 * Build journalctl args array for exec_command().
 * If unit is NULL — global journal (no -u flag).
 *
 * @param argv             Output args array
 * @param argv_size        Size of args array
 * @param sudo_path        Path to sudo binary
 * @param journalctl_path  Path to journalctl binary
 * @param unit             Systemd unit name, or NULL for global journal
 * @param lines_str        Number of lines as string (e.g. "30")
 * @return                 Number of args, or -1 on error
 */
static int build_journalctl_args(char **argv, size_t argv_size,
                                  const char *sudo_path,
                                  const char *journalctl_path,
                                  const char *unit,
                                  const char *lines_str)
{
    int i = 0;

    if (argv_size < 9) return -1;

    argv[i++] = (char *)sudo_path;
    argv[i++] = "-n";
    argv[i++] = (char *)journalctl_path;

    if (unit) {
        if ((size_t)i + 4 > argv_size) return -1;
        argv[i++] = "-u";
        argv[i++] = (char *)unit;
    }

    argv[i++] = "-n";
    argv[i++] = (char *)lines_str;
    argv[i++] = "--no-pager";
    argv[i]   = NULL;

    return i;
}

// ============================================================================
// LOG OUTPUT PROCESSING
// ============================================================================

/**
 * Process raw journalctl output into formatted Telegram response
 * 
 * Handles:
 *   - Multi-keyword filtering (case-insensitive)
 *   - Reboot marker detection
 *   - ANSI escape code stripping
 *   - Journal header suppression
 *   - Truncation when buffer limit reached
 * 
 * @param tmp         Raw output from journalctl
 * @param buffer      Output buffer for formatted response
 * @param size        Size of output buffer
 * @param svc         Service alias being queried (for display)
 * @param lines       Number of lines requested
 * @param filter      Filter string (may be NULL)
 * @param is_fallback 1 if this is fallback (global journal), 0 otherwise
 * @param out_used    Output: final write position in buffer (may be NULL)
 * @return            Result with matched and raw line counts
 */
static logs_result_t process_logs_output(char *tmp,
                                         char *buffer,
                                         size_t size,
                                         const char *svc,
                                         int lines,
                                         const char *filter,
                                         int is_fallback,
                                         size_t *out_used)
{
    LOG_CMD(LOG_DEBUG,
        "process_logs_output start: svc=%s filter=%s is_fallback=%d",
        svc, filter ? filter : "NULL", is_fallback);

    buffer[0] = '\0';

    int hdr = snprintf(buffer, size,
        "📜 LOGS: %s (%s%d lines)%s\n\n",
        svc,
        is_fallback ? "fallback, " : "",
        lines,
        filter ? " [filtered]" : "");

    size_t used = (hdr > 0 && (size_t)hdr < size) ? (size_t)hdr : 0;

    int line_count = 0;
    int dropped    = 0;
    int raw_lines  = 0;

    /* Prepare multi-keyword filter */
    filter_t f = {0};
    if (filter) {
        char filter_copy[128];
        snprintf(filter_copy, sizeof(filter_copy), "%s", filter);
        filter_copy[sizeof(filter_copy) - 1] = '\0';
        parse_filter(filter_copy, &f);
    }

    char *saveptr;
    char *line = strtok_r(tmp, "\n", &saveptr);

    while (line) {
        raw_lines++;

        if (raw_lines <= DEBUG_LINES)
            LOG_CMD(LOG_DEBUG, "raw[%d]: %s", raw_lines, line);

        /* Prevent Telegram message size limit issues */
        if (used > LOGS_BUFFER_SOFT_CAP) {
            used = append_at(buffer, size, used,
                "\n...\n⚠ logs truncated (limit reached)");
            LOG_CMD(LOG_WARN,
                "logs truncated early (%s): raw=%d matched=%d",
                is_fallback ? "fallback" : svc, raw_lines, line_count);
            break;
        }

        sanitize_line(line);

        /* Highlight reboot markers */
        if (strstr(line, "-- Reboot --")) {
            used = append_at(buffer, size, used, "\n=== REBOOT ===\n");
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        /* Suppress journald header line */
        if (strstr(line, "Logs begin at")) {
            LOG_CMD(LOG_DEBUG, "Skipping journal header line: %s", line);
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        /* Strip ANSI escape codes */
        char *esc = strchr(line, '\x1b');
        if (esc)
            *esc = '\0';

        /* Apply multi-keyword filter */
        if (!match_filter_multi(line, &f)) {
            dropped++;
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        if (line_count < DEBUG_LINES) {
            LOG_CMD(LOG_DEBUG,
                "%s LOG LINE[%d]: %s",
                is_fallback ? "FALLBACK" : "LOG",
                line_count, line);
        }

        line_count++;

        /* Check buffer capacity before appending */
        if (used + strlen(line) >= size - 5) {
            LOG_CMD(LOG_WARN,
                "buffer limit reached (svc=%s, matched=%d)", svc, line_count);
            break;
        }

        used = append_at(buffer, size, used, line);
        used = append_at(buffer, size, used, "\n");

        line = strtok_r(NULL, "\n", &saveptr);
    }

    LOG_CMD(LOG_DEBUG,
        "logs_done: svc=%s raw=%d matched=%d dropped=%d bytes=%zu",
        svc, raw_lines, line_count, dropped, used);

    if (line_count == 0) {
        LOG_CMD(LOG_DEBUG,
            "no matches (svc=%s filter=%s)", svc, filter ? filter : "NULL");
    }

    if (out_used) *out_used = used;

    logs_result_t res = {
        .matched = line_count,
        .raw     = raw_lines
    };

    return res;
}

// ============================================================================
// PUBLIC API: LOGS COMMAND HANDLER
// ============================================================================

/**
 * Main entry point for /logs command
 * 
 * Parses arguments, validates service and filter, executes journalctl,
 * and formats the response.
 * 
 * Usage:
 *   /logs                          - List available services
 *   /logs <service>                - Show last 30 lines
 *   /logs <service> <lines>        - Show last N lines (max 200)
 *   /logs <service> <lines> <filter> - Filter results
 * 
 * @param service  Command arguments string
 * @param buffer   Output buffer for Telegram response
 * @param size     Size of output buffer
 * @param req_id   16-bit request identifier for log correlation
 * @return         0 on success, -1 on error
 */
int logs_get(const char *service, char *buffer, size_t size, unsigned short req_id) {

    LOG_STATE(LOG_INFO, "req=%04x logs_get() called with service='%s'",
        req_id, service ? service : "NULL");

    // ------------------------------------------------------------------------
    // No arguments: list available services from shared table
    // ------------------------------------------------------------------------
    if (!service || service[0] == '\0') {
        buffer[0] = '\0';
        safe_append(buffer, size, "📜 Available services:\n\n");

        for (int i = 0; i < g_services_count; i++) {
            safe_append(buffer, size, "- ");
            safe_append(buffer, size, g_services[i].alias);
            safe_append(buffer, size, "\n");
        }

        safe_append(buffer, size,
            "\nUsage:\n"
            "/logs <service>\n"
            "/logs <service> <lines>\n"
            "/logs <service> <lines> <filter>\n");

        return 0;
    }

    // ------------------------------------------------------------------------
    // Parse arguments: service [lines] [filter]
    // ------------------------------------------------------------------------
    char svc[64]  = {0};
    char arg1[64] = {0};
    char arg2[64] = {0};

    int parsed = sscanf(service, "%63s %63s %63s", svc, arg1, arg2);

    /* Resolve alias → systemd unit via shared table */
    const char *real_service = resolve_service(svc);
    if (!real_service) {
        snprintf(buffer, size, "❌ Service not allowed");
        LOG_SEC(LOG_WARN, "req=%04x blocked logs access: %s", req_id, svc);
        return -1;
    }

    int         lines  = 30;
    const char *filter = NULL;

    if (parsed >= 2) {
        if (is_number(arg1)) {
            if (parse_int(arg1, &lines) != 0) {
                snprintf(buffer, size, "❌ Invalid number");
                LOG_CMD(LOG_WARN, "req=%04x invalid number: %s", req_id, arg1);
                return -1;
            }
        } else {
            filter = arg1;
        }
    }

    if (parsed >= 3)
        filter = arg2;

    // ------------------------------------------------------------------------
    // Validate filter string
    // ------------------------------------------------------------------------
    if (filter) {
        size_t flen = strlen(filter);

        if (flen > 32) {
            LOG_CMD(LOG_WARN, "req=%04x filter too long: %s", req_id, filter);
            snprintf(buffer, size, "❌ Filter too long");
            return -1;
        }

        if (flen < 3) {
            LOG_CMD(LOG_WARN, "req=%04x filter too short: %s", req_id, filter);
            snprintf(buffer, size, "❌ Filter too short");
            return -1;
        }

        for (size_t i = 0; i < flen; i++) {
            char c = filter[i];

            if (!isalnum((unsigned char)c) &&
                c != '-' && c != '_' && c != '.') {
                LOG_CMD(LOG_WARN,
                        "req=%04x invalid filter rejected: %s (bad char: %c)",
                        req_id, filter, c);
                snprintf(buffer, size, "❌ Invalid filter");
                return -1;
            }
        }

        LOG_CMD(LOG_DEBUG, "req=%04x filter accepted: %s", req_id, filter);
    }

    /* Clamp line count to reasonable range */
    if (lines <= 0)
        lines = 30;

    if (lines > LOGS_MAX_LINES) {
        LOG_CMD(LOG_WARN, "req=%04x lines clamped: %d -> %d",
                req_id, lines, LOGS_MAX_LINES);
        lines = LOGS_MAX_LINES;
    }

    // ------------------------------------------------------------------------
    // Execute journalctl
    // ------------------------------------------------------------------------
    char lines_str[16];
    snprintf(lines_str, sizeof(lines_str), "%d", lines);

    char  *argv[JOURNALCTL_ARGS_MAX] = {0};
    char   tmp[8192]                 = {0};
    size_t used                      = 0;

    build_journalctl_args(argv, JOURNALCTL_ARGS_MAX,
        g_cfg.sudo_path, g_cfg.journalctl_path,
        real_service, lines_str);

    exec_result_t exec_res;
    int rc = exec_command(argv, tmp, sizeof(tmp), NULL, &exec_res);
    if (rc != 0) {
        LOG_EXEC(LOG_DEBUG,
            "req=%04x RAW OUTPUT (%s): first 200 chars:\n%.200s",
            req_id, svc, tmp);
        LOG_EXEC(LOG_ERROR, "req=%04x journalctl exec failed", req_id);
        snprintf(buffer, size, "❌ Failed to read logs");
        return -1;
    }

    logs_result_t res = process_logs_output(
        tmp, buffer, size, svc, lines, filter, 0 /* not fallback */, &used);

    // ------------------------------------------------------------------------
    // Fallback: try global journal if service has no logs
    // ------------------------------------------------------------------------
    if (res.raw == 0) {
        LOG_CMD(LOG_WARN,
            "req=%04x no logs for %s, trying global journal", req_id, svc);

        build_journalctl_args(argv, JOURNALCTL_ARGS_MAX,
            g_cfg.sudo_path, g_cfg.journalctl_path,
            NULL /* global journal */, lines_str);

        exec_result_t res_fallback;
        if (exec_command(argv, tmp, sizeof(tmp), NULL, &res_fallback) != 0) {
            LOG_EXEC(LOG_ERROR,
                "req=%04x fallback journalctl exec failed", req_id);
            snprintf(buffer, size, "❌ No logs available");
            return -1;
        }

        res = process_logs_output(
            tmp, buffer, size, svc, lines, filter, 1 /* is fallback */, &used);

        LOG_CMD(LOG_INFO,
            "req=%04x fallback done: svc=%s matched=%d raw=%d bytes=%zu",
            req_id, svc, res.matched, res.raw, used);
    }

    if (res.raw > 0 && res.matched == 0) {
        LOG_CMD(LOG_INFO,
            "req=%04x no matches: svc=%s filter=%s",
            req_id, svc, filter ? filter : "NULL");
    }

    // ------------------------------------------------------------------------
    // Handle empty results
    // ------------------------------------------------------------------------
    if (res.matched == 0) {
        if (filter) {
            snprintf(buffer, size,
                "📜 LOGS: %s\n\n"
                "No matches for filter: \"%s\"\n"
                "(scanned %d lines)\n\n"
                "Try:\n"
                "- /logs %s\n"
                "- /logs %s fail\n"
                "- /logs %s Accepted\n",
                svc, filter, res.raw,
                svc, svc, svc);
        } else {
            snprintf(buffer, size,
                "📜 LOGS: %s\n\nNo logs found", svc);
        }
    }

    LOG_CMD(LOG_DEBUG,
        "req=%04x response size: %zu bytes", req_id, strlen(buffer));

    return 0;
}