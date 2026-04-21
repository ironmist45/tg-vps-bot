/**
 * tg-bot - Telegram bot for system administration
 * cmd_help.h - Help command handler (/help)
 * MIT License - Copyright (c) 2026
 */

#ifndef CMD_HELP_H
#define CMD_HELP_H

#include "commands.h"

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Display list of available commands grouped by category (V2 handler)
 * 
 * Iterates through the global commands[] table and formats a categorized
 * help message. Commands with category == NULL are hidden.
 * 
 * If a command has a description, it's displayed next to the command name.
 * 
 * Output format (Markdown):
 *   *📚 COMMANDS*
 *   
 *   *General*
 *   /start
 *   /help
 *   
 *   *System*
 *   /status — System status
 *   /health — Health check
 * 
 * @param ctx  Command context
 * @return     0 on success
 */
int cmd_help_v2(command_ctx_t *ctx);

#endif // CMD_HELP_H
