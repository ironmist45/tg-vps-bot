/**
 * tg-bot - Telegram bot for system administration
 * services.h - Systemd service status monitoring
 * MIT License - Copyright (c) 2026 ironmist45
 */

#ifndef SERVICES_H
#define SERVICES_H

#include <stddef.h>

// ============================================================================
// SERVICE ACTION
// ============================================================================

/**
 * Actions that can be performed on a single service.
 * Used by service_action() and cmd_service_v2().
 */
typedef enum {
    SERVICE_ACTION_STATUS,   /* systemctl is-active <unit>  */
    SERVICE_ACTION_START,    /* systemctl start <unit>      */
    SERVICE_ACTION_STOP,     /* systemctl stop <unit>       */
    SERVICE_ACTION_RESTART   /* systemctl restart <unit>    */
} service_action_t;

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Get formatted status of all whitelisted services
 *
 * Queries systemd for the status of predefined services and formats
 * the result for Telegram display.
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
 * Status indicators:
 *   🟢 UP        - service is active
 *   🔴 DOWN      - service is inactive
 *   🟡 FAIL      - service failed
 *   🟡 STARTING  - service is activating
 *   ⚪ UNKNOWN   - status could not be determined
 *
 * @param buffer  Output buffer for formatted response
 * @param size    Size of output buffer
 * @param req_id  16-bit request identifier for log correlation
 * @return        0 on success, -1 on error
 */
int services_get_status(char *buffer, size_t size, unsigned short req_id);

/**
 * Perform an action on a single whitelisted service.
 *
 * Looks up the service by alias in the shared g_services table,
 * validates the unit name, and executes the requested systemctl command.
 *
 * For SERVICE_ACTION_STATUS the output buffer contains the formatted
 * status string (e.g. "SSH: 🟢 UP"). For start/stop/restart it contains
 * the raw systemctl output (may be empty on success — that is normal).
 *
 * @param alias    User-facing service alias (e.g. "ssh", "mtg")
 * @param action   Action to perform
 * @param out      Output buffer for result string
 * @param out_size Size of output buffer
 * @param req_id   16-bit request identifier for log correlation
 * @return         0 on success, -1 on error (unknown alias, exec failure)
 */
int service_action(const char *alias, service_action_t action,
                   char *out, size_t out_size, unsigned short req_id);

#endif // SERVICES_H
