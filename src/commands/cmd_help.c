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
    char buffer[2048];
    size_t size = sizeof(buffer);
    
    // Write header
    int written = snprintf(buffer, size, "*📚 COMMANDS*\n\n");
    if (written < 0 || (size_t)written >= size) {
        return reply_error(ctx, "Response too long");
    }

    size_t used = written;
    const char *current_category = NULL;

    for (int i = 0; i < commands_count; i++) {
        if (used >= size - 1) break;
        if (!commands[i].category) continue;

        if (!current_category ||
            strcmp(current_category, commands[i].category) != 0) {
            written = snprintf(buffer + used, size - used,
                "%s*%s*\n",
                current_category ? "\n" : "",
                commands[i].category);
            if (written < 0 || (size_t)written >= size - used) break;
            used += written;
            current_category = commands[i].category;
        }

        const char *name = commands[i].name;
        const char *desc = commands[i].description;
        
        if (desc && *desc) {
            written = snprintf(buffer + used, size - used,
                "%s — _%s_\n", name, desc);
        } else {
            written = snprintf(buffer + used, size - used, "%s\n", name);
        }
        if (written < 0 || (size_t)written >= size - used) break;
        used += written;
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "help: displayed %zu bytes", used);
    
    // ✅ Теперь можно использовать reply_markdown
    return reply_markdown(ctx, buffer);
}
