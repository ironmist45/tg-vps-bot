/**
 * tg-bot - Telegram bot for system administration
 * cmd_services.h - Service-related command handlers (/services, /users, /logs)
 * MIT License - Copyright (c) 2026
 */

#ifndef CMD_SERVICES_H
#define CMD_SERVICES_H

#include <stddef.h>
#include "commands.h"

// ============================================================================
// PUBLIC API (V2 HANDLERS)
// ============================================================================

/**
 * Display status of all whitelisted systemd services
 * 
 * Output includes emoji indicators:
 *   🟢 UP, 🔴 DOWN, 🟡 FAIL/STARTING, ⚪ UNKNOWN
 * 
 * @param ctx  Command context
 * @return     0 on success, error reply on failure
 */
int cmd_services_v2(command_ctx_t *ctx);

/**
 * Display currently logged-in users
 * 
 * Shows username, TTY, login host, and login time for each active session.
 * Output format: Markdown with user details.
 * 
 * @param ctx  Command context
 * @return     0 on success, error reply on failure
 */
int cmd_users_v2(command_ctx_t *ctx);

/**
 * View systemd journal logs with filtering
 * 
 * Usage:
 *   /logs                       - Show available services
 *   /logs <service>             - Last 30 lines
 *   /logs <service> <N>         - Last N lines (max 200)
 *   /logs <service> <filter>    - Filter results (error, auth, brute, ip, etc.)
 * 
 * Allowed services: ssh, mtg, shadowsocks
 * Output is plain text (may contain raw journal output).
 * 
 * @param ctx  Command context
 * @return     0 on success, response type set to RESP_PLAIN
 */
int cmd_logs_v2(command_ctx_t *ctx);

#endif // CMD_SERVICES_H
