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
 * Display list of available commands grouped by category
 * 
 * Iterates through the global commands[] table and formats a categorized
 * help message. Commands with category == NULL are hidden.
 * 
 * Output format (Markdown):
 *   *📚 COMMANDS*
 *   
 *   *General*
 *   /start
 *   /help
 *   
 *   *System*
 *   /status
 *   /health
 *   /ping
 * 
 * @param argc       Argument count (unused)
 * @param argv       Argument vector (unused)
 * @param chat_id    Chat ID of requester (unused)
 * @param response   Response buffer
 * @param resp_size  Size of response buffer
 * @param resp_type  Output: response type (always RESP_MARKDOWN)
 * @return           0 on success, -1 on error
 */
int cmd_help(int argc, char *argv[],
             long chat_id,
             char *response,
             size_t resp_size,
             response_type_t *resp_type);

#endif // CMD_HELP_H
