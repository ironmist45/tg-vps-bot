/**
 * tg-bot - Telegram bot for system administration
 * cmd_services.h - Service-related command handlers (/services, /users, /logs, /service)
 * MIT License - Copyright (c) 2026 inronmist45
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

/**
 * Perform an action on a single whitelisted service
 *
 * Usage:
 *   /service <alias> <action>
 *
 * Aliases:  ssh, shadowsocks, mtg (as defined in services_config.c)
 * Actions:  status, start, stop, restart
 *
 * status   — returns immediately with emoji indicator, no confirmation
 * start    — requires two-step confirmation (TOTP or token)
 * stop     — requires two-step confirmation (TOTP or token)
 * restart  — requires two-step confirmation (TOTP or token)
 *
 * The handler itself decides whether confirmation is needed based on
 * the action — requires_confirmation = 0 in the command table.
 *
 * @param ctx  Command context (ctx->args = "ssh restart" etc.)
 * @return     0 on success, error reply on failure
 */
int cmd_service_v2(command_ctx_t *ctx);

#endif // CMD_SERVICES_H
