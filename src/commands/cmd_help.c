/**
 * tg-bot - Telegram bot for system administration
 * cmd_help.c - /help command handler
 * MIT License - Copyright (c) 2026
 */

#include "cmd_help.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>

// Command table from commands.c
extern command_t commands[];
extern const int commands_count;

// ============================================================================
// /help COMMAND
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
 * @param argc       Argument count (unused)
 * @param argv       Argument vector (unused)
 * @param chat_id    Chat ID of requester (unused)
 * @param resp       Response buffer
 * @param size       Size of response buffer
 * @param resp_type  Output: response type (always RESP_MARKDOWN)
 * @return           0 on success, -1 on error
 */
int cmd_help(int argc, char *argv[],
             long chat_id,
             char *resp, size_t size,
             response_type_t *resp_type) {
  
    (void)argc;
    (void)argv;
    (void)chat_id;

    if (resp_type) *resp_type = RESP_MARKDOWN;

    // Write header
    int written = snprintf(resp, size, "*📚 COMMANDS*\n\n");
    if (written < 0 || (size_t)written >= size)
        return -1;

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

            int written = snprintf(resp + used, size - used,
                "%s*%s*\n",
                current_category ? "\n" : "",
                commands[i].category);

            if (written < 0 || (size_t)written >= size - used)
                break;

            used += written;
            current_category = commands[i].category;
        }

        // Write command name
        const char *name = commands[i].name;
        int written = snprintf(resp + used, size - used, "%s\n", name);

        if (written < 0 || (size_t)written >= size - used)
            break;

        used += written;
    }

    LOG_STATE(LOG_DEBUG, "help: building command list");
    LOG_STATE(LOG_INFO, "help generated (len=%zu)", used);
    
    return 0;
}
