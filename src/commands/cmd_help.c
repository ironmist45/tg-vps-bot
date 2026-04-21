/**
 * tg-bot - Telegram bot for system administration
 * cmd_help.c - /help command handler (V2)
 * MIT License - Copyright (c) 2026
 */

#include "cmd_help.h"
#include "logger.h"
#include "reply.h"

#include <stdio.h>
#include <string.h>

// Command table from commands.c
extern command_t commands[];
extern const int commands_count;

// ============================================================================
// /help COMMAND (V2)
// ============================================================================

/**
 * Display list of available commands grouped by category
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
 * Hidden commands (category == NULL) are not displayed.
 * 
 * @param ctx  Command context
 * @return     0 on success
 */
int cmd_help_v2(command_ctx_t *ctx)
{
    char *resp = ctx->response;
    size_t size = ctx->resp_size;

    // Write header
    int written = snprintf(resp, size, "*📚 COMMANDS*\n\n");
    if (written < 0 || (size_t)written >= size) {
        return reply_error(ctx, "Response too long");
    }

    size_t used = written;
    const char *current_category = NULL;

    // Iterate through command table
    for (int i = 0; i < commands_count; i++) {
        // Prevent buffer overflow
        if (used >= size - 1)
            break;

        // Skip hidden commands
        if (!commands[i].category)
            continue;

        // New category header
        if (!current_category ||
            strcmp(current_category, commands[i].category) != 0) {

            written = snprintf(resp + used, size - used,
                "%s*%s*\n",
                current_category ? "\n" : "",
                commands[i].category);

            if (written < 0 || (size_t)written >= size - used)
                break;

            used += written;
            current_category = commands[i].category;
        }

        // Write command name with description if available
        const char *name = commands[i].name;
        const char *desc = commands[i].description;
        
        if (desc && *desc) {
            written = snprintf(resp + used, size - used,
                "%s — _%s_\n", name, desc);
        } else {
            written = snprintf(resp + used, size - used, "%s\n", name);
        }

        if (written < 0 || (size_t)written >= size - used)
            break;

        used += written;
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "help: displayed %zu bytes", used);
    
    return reply_markdown(ctx, resp);
}
