/* tg-bot — services.c — Systemd service status monitoring. MIT License © 2026 ironmist45 */

/*
 * Provides the /services command for checking the status of
 * whitelisted systemd services, and service_action() for
 * start/stop/restart of individual services.
 *
 * Features:
 *   - Whitelist-based access control (only predefined services)
 *   - Service name validation (prevents injection)
 *   - Status detection: active, inactive, failed, activating
 *   - Emoji indicators: 🟢 UP, 🔴 DOWN, 🟡 FAIL/STARTING, ⚪ UNKNOWN
 *   - Formatted output in Markdown code block
 *
 * Service list is defined in services_config.c — edit that file to
 * add, remove, or rename monitored services.
 */

#include "exec.h"
#include "services.h"
#include "services_config.h"
#include "config.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

// ============================================================================
// SERVICE NAME VALIDATION
// ============================================================================

/**
 * Validate service name format (prevent command injection)
 *
 * Allowed characters: alphanumeric, hyphen, underscore.
 * Unit names in services_config.c do not include ".service" suffix,
 * so dot is intentionally excluded.
 *
 * @param s  Service name to validate
 * @return   1 if valid, 0 otherwise
 */
static int is_valid_service_name(const char *s) {
    if (!s || *s == '\0') return 0;

    for (size_t i = 0; s[i]; i++) {
        char c = s[i];
        if (!(isalnum((unsigned char)c) || c == '-' || c == '_'))
            return 0;
    }

    return 1;
}

// ============================================================================
// ALIAS LOOKUP
// ============================================================================

/**
 * Find a service definition by user-facing alias.
 *
 * Searches the shared g_services table (services_config.c).
 *
 * @param alias  User-provided alias (e.g. "ssh", "shadowsocks")
 * @return       Pointer to matching service_def_t, or NULL if not found
 */
static const service_def_t *find_service_by_alias(const char *alias) {
    if (!alias) return NULL;

    for (int i = 0; i < g_services_count; i++) {
        if (strcmp(g_services[i].alias, alias) == 0)
            return &g_services[i];
    }

    return NULL;
}

// ============================================================================
// SERVICE STATUS QUERY
// ============================================================================

/**
 * Get current status of a single service via systemctl
 *
 * Executes: sudo -n systemctl is-active <unit>
 *
 * @param unit  Systemd unit name (must be validated)
 * @param out   Output buffer for status string
 * @param size  Size of output buffer
 * @return      0 on success, -1 on error
 */
static int get_service_status(const char *unit, char *out, size_t size) {
    if (!is_valid_service_name(unit)) {
        LOG_CMD(LOG_ERROR, "Invalid service unit name: %s", unit);
        if (size > 0) snprintf(out, size, "invalid");
        return -1;
    }

    char *const args[] = {
        (char *)g_cfg.sudo_path,
        "-n",
        (char *)g_cfg.systemctl_path,
        "is-active",
        (char *)unit,
        NULL
    };

    exec_opts_t opts = {
        .timeout_ms = 2000,
        .quiet      = 1
    };

    exec_result_t res;
    int rc = exec_command(args, out, size, &opts, &res);

    if (rc != 0) {
        if (res.status == EXEC_TIMEOUT)
            LOG_CMD(LOG_ERROR, "service %s: timeout", unit);
        else
            LOG_CMD(LOG_ERROR, "service %s: exec failed (%s)",
                    unit, exec_status_str(res.status));

        if (size > 0) snprintf(out, size, "unknown");
        return -1;
    }

    if (res.exit_code != 0)
        LOG_CMD(LOG_DEBUG, "service %s: non-zero exit (%d)", unit, res.exit_code);

    /* Remove trailing newline from systemctl output */
    out[strcspn(out, "\n")] = '\0';

    /* Handle empty output */
    if (out[0] == '\0') {
        snprintf(out, size, "unknown");
        return 0;
    }

    /* Trim leading spaces (defensive) */
    char *p = out;
    while (*p == ' ') p++;
    if (p != out)
        memmove(out, p, strlen(p) + 1);

    LOG_CMD(LOG_DEBUG, "service=%s status=%s", unit, out);

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
    if (strcmp(status, "active")     == 0) return "🟢 UP";
    if (strcmp(status, "inactive")   == 0) return "🔴 DOWN";
    if (strcmp(status, "failed")     == 0) return "🟡 FAIL";
    if (strcmp(status, "activating") == 0) return "🟡 STARTING";
    return "⚪ UNKNOWN";
}

// ============================================================================
// PUBLIC API: ALL SERVICES STATUS
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
    LOG_CMD(LOG_INFO,  "req=%04x services_get_status()", req_id);
    LOG_CMD(LOG_DEBUG, "req=%04x services count: %d", req_id, g_services_count);

    if (!buffer || size == 0) {
        LOG_CMD(LOG_ERROR, "req=%04x invalid buffer", req_id);
        return -1;
    }

    buffer[0] = '\0';
    size_t offset = 0;

    // ------------------------------------------------------------------------
    // Header
    // ------------------------------------------------------------------------
    int written = snprintf(buffer + offset, size - offset,
                           "*🧩 SERVICES*\n\n```\n");

    if (written < 0) {
        LOG_CMD(LOG_ERROR, "snprintf error (header)");
        return -1;
    }

    if ((size_t)written >= size - offset) {
        LOG_CMD(LOG_WARN, "services buffer truncated (header)");
        return 0;
    }

    offset += (size_t)written;

    // ------------------------------------------------------------------------
    // One row per service: query systemctl via g_services[i].unit,
    // display via g_services[i].display
    // ------------------------------------------------------------------------
    for (int i = 0; i < g_services_count; i++) {
        char status[64] = {0};

        if (get_service_status(g_services[i].unit,
                               status, sizeof(status)) != 0) {
            LOG_CMD(LOG_WARN, "Failed to get status: %s", g_services[i].unit);
        }

        const char *status_fmt = format_status(status);

        LOG_CMD(LOG_DEBUG, "service=%s display=%s status=%s",
                g_services[i].unit, g_services[i].display, status_fmt);

        written = snprintf(buffer + offset, size - offset,
                           "%-16s : %s\n",
                           g_services[i].display, status_fmt);

        if (written < 0) {
            LOG_CMD(LOG_ERROR, "snprintf error (line)");
            break;
        }

        if ((size_t)written >= size - offset) {
            LOG_CMD(LOG_WARN, "services buffer limit reached (offset=%zu size=%zu)",
                    offset, size);
            break;
        }

        offset += (size_t)written;
    }

    // ------------------------------------------------------------------------
    // Footer
    // ------------------------------------------------------------------------
    int written_end = snprintf(buffer + offset, size - offset, "\n```");

    if (written_end < 0) {
        LOG_CMD(LOG_ERROR, "snprintf error (footer)");
    } else if ((size_t)written_end >= size - offset) {
        LOG_CMD(LOG_WARN, "services buffer truncated (footer)");
    } else {
        offset += (size_t)written_end;
    }

    LOG_CMD(LOG_DEBUG, "req=%04x services response ready: %zu bytes",
            req_id, offset);

    return 0;
}

// ============================================================================
// PUBLIC API: SINGLE SERVICE ACTION
// ============================================================================

/**
 * Perform an action on a single whitelisted service.
 *
 * Resolves alias → unit via g_services table, validates the unit name,
 * then executes the appropriate systemctl subcommand.
 *
 * For SERVICE_ACTION_STATUS: out receives the emoji status string.
 * For start/stop/restart:    out receives raw systemctl output (may be
 *                            empty on success — that is normal).
 *
 * @param alias    User-facing alias (e.g. "ssh", "mtg")
 * @param action   Action to perform
 * @param out      Output buffer for result
 * @param out_size Size of output buffer
 * @param req_id   Request ID for log correlation
 * @return         0 on success, -1 on error
 */
int service_action(const char *alias, service_action_t action,
                   char *out, size_t out_size, unsigned short req_id)
{
    if (!alias || !out || out_size == 0) return -1;

    out[0] = '\0';

    /* Resolve alias → service definition */
    const service_def_t *svc = find_service_by_alias(alias);
    if (!svc) {
        LOG_CMD(LOG_WARN, "req=%04x service_action: unknown alias '%s'",
                req_id, alias);
        snprintf(out, out_size, "❌ Unknown service: %s", alias);
        return -1;
    }

    /* Validate unit name before passing to exec */
    if (!is_valid_service_name(svc->unit)) {
        LOG_CMD(LOG_ERROR, "req=%04x service_action: invalid unit name '%s'",
                req_id, svc->unit);
        snprintf(out, out_size, "❌ Invalid service unit");
        return -1;
    }

    /* STATUS — use existing get_service_status and format with emoji */
    if (action == SERVICE_ACTION_STATUS) {
        char raw[64] = {0};
        int rc = get_service_status(svc->unit, raw, sizeof(raw));

        const char *fmt = format_status(raw);
        snprintf(out, out_size, "%s: %s", svc->display, fmt);

        LOG_CMD(LOG_DEBUG, "req=%04x service status: alias=%s unit=%s status=%s",
                req_id, alias, svc->unit, raw);
        return rc;
    }

    /* START / STOP / RESTART */
    const char *subcmd = NULL;
    switch (action) {
        case SERVICE_ACTION_START:   subcmd = "start";   break;
        case SERVICE_ACTION_STOP:    subcmd = "stop";    break;
        case SERVICE_ACTION_RESTART: subcmd = "restart"; break;
        default:
            snprintf(out, out_size, "❌ Unknown action");
            return -1;
    }

    LOG_CMD(LOG_INFO, "req=%04x service_action: %s %s (unit=%s)",
            req_id, subcmd, alias, svc->unit);

    char *const args[] = {
        (char *)g_cfg.sudo_path,
        "-n",
        (char *)g_cfg.systemctl_path,
        (char *)subcmd,
        (char *)svc->unit,
        NULL
    };

    exec_opts_t opts = {
        .timeout_ms = 10000,  /* allow up to 10s for start/stop/restart */
        .quiet      = 1
    };

    exec_result_t res;
    int rc = exec_command(args, out, out_size, &opts, &res);

    if (rc != 0 || res.exit_code != 0) {
        LOG_CMD(LOG_ERROR,
                "req=%04x service_action: %s %s failed (status=%s exit=%d)",
                req_id, subcmd, alias,
                exec_status_str(res.status), res.exit_code);
        return -1;
    }

    LOG_CMD(LOG_INFO, "req=%04x service_action: %s %s OK (%ldms)",
            req_id, subcmd, alias, res.duration_ms);

    return 0;
}
