/**
 * tg-bot - Telegram bot for system administration
 * cmd_system.h - System information command handlers (/start, /status, /health, /about, /ping)
 * MIT License - Copyright (c) 2026
 */

#ifndef CMD_SYSTEM_H
#define CMD_SYSTEM_H

#include <stddef.h>
#include "commands.h"

// ============================================================================
// PUBLIC API (V2 HANDLERS)
// ============================================================================

/**
 * Bot introduction and welcome message
 * 
 * Displays bot version, personalized greeting, system summary, and quick navigation.
 * 
 * @param ctx  Command context
 * @return     0 on success
 */
int cmd_start_v2(command_ctx_t *ctx);

/**
 * Display detailed system status
 * 
 * Shows OS info, CPU load, memory/disk usage with progress bars,
 * system uptime, and active user count.
 * Output format: Markdown code block.
 * 
 * @param ctx  Command context
 * @return     0 on success, error reply on failure
 */
int cmd_status_v2(command_ctx_t *ctx);

/**
 * Quick system health check (compact format)
 * 
 * Shows essential metrics with emoji heat indicators:
 * 🟢 normal, 🟡 warning, 🔴 critical
 * 
 * @param ctx  Command context
 * @return     0 on success, error reply on failure
 */
int cmd_health_v2(command_ctx_t *ctx);

/**
 * Display bot version and runtime information
 * 
 * Shows application name, version, codename, PID, and bot uptime.
 * 
 * @param ctx  Command context
 * @return     0 on success
 */
int cmd_about_v2(command_ctx_t *ctx);

/**
 * Network and processing latency test
 * 
 * Measures and reports:
 *   - Processing time: command handler execution time
 *   - Inbound latency: Telegram → Bot (estimated)
 *   - RTT (estimated): round-trip time
 *   - Bot uptime
 * 
 * @param ctx  Command context (uses msg_date for latency calculation)
 * @return     0 on success
 */
int cmd_ping_v2(command_ctx_t *ctx);

#endif // CMD_SYSTEM_H
