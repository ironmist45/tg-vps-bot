/**
 * tg-bot - Telegram bot for system administration
 * cmd_help.c - /help command handler (V2)
 * MIT License - Copyright (c) 2026 ironmist45
 */
#include "cmd_help.h"
#include "telegram_parser.h"
#include "logger.h"
#include "reply.h"
#include "metrics.h"
#include <stdio.h>
#include <string.h>

/* Command table from commands.c */
extern command_t commands[];
extern const int commands_count;

// ============================================================================
// /help COMMAND (V2)
// ============================================================================

/**
 * Display list of available commands grouped by category.
 *
 * Output format (MarkdownV2):
 *   *📚 COMMANDS*
 *
 *   *── General*
 *   /start
 *   /help — Show this help
 *
 *   *── Server*
 *   /status — Full system status
 *
 * Hidden commands (category == NULL) are not displayed.
 *
 * Buffer analysis (current command table, worst-case escaping):
 *   Maximum output: ~601 bytes out of 2048 — headroom for ~25 more commands.
 *   safe_name[128]: accommodates names up to 64 chars after MarkdownV2 escaping.
 *   safe_desc[256]: accommodates descriptions up to 128 chars after escaping.
 *
 * @param ctx  Command context
 * @return     0 on success
 */
int cmd_help_v2(command_ctx_t *ctx)
{
    char buffer[2048];
    size_t size = sizeof(buffer);

    /* Write header */
    int written = snprintf(buffer, size, "*📚 COMMANDS*\n\n");
    if (written < 0 || (size_t)written >= size) {
        return reply_error(ctx, "Response too long");
    }
    size_t used = written;

    const char *current_category = NULL;

    for (int i = 0; i < commands_count; i++) {
        if (used >= size - 1) break;
        if (!commands[i].category) continue;

        /* Category header — printed once per category group */
        if (!current_category ||
            strcmp(current_category, commands[i].category) != 0) {
            written = snprintf(buffer + used, size - used,
                "%s*── %s*\n",
                current_category ? "\n" : "",
                commands[i].category);
            if (written < 0 || (size_t)written >= size - used) {
                LOG_CMD_CTX(ctx, LOG_WARN,
                    "help: buffer full at category '%s'",
                    commands[i].category);
                break;
            }
            used += written;
            current_category = commands[i].category;
        }

        /*
         * Escape command name and description for MarkdownV2.
         *
         * safe_name[128]: worst-case is every character escaped (×2),
         *   so supports names up to 64 characters — well above any real command.
         *
         * safe_desc[256]: same logic, supports descriptions up to 128 characters.
         */
        const char *name = commands[i].name;
        const char *desc = commands[i].description;

        char safe_name[128] = {0};
        char safe_desc[256] = {0};

        telegram_escape_markdown(name, safe_name, sizeof(safe_name));

        if (desc && *desc) {
            telegram_escape_markdown(desc, safe_desc, sizeof(safe_desc));
            written = snprintf(buffer + used, size - used,
                "%s%s — %s\n",
                commands[i].requires_confirmation ? "🔐 " : "",
                safe_name, safe_desc);
        } else {
            written = snprintf(buffer + used, size - used,
                "%s%s\n",
                commands[i].requires_confirmation ? "🔐 " : "",
                safe_name);
        }

        if (written < 0 || (size_t)written >= size - used) {
            LOG_CMD_CTX(ctx, LOG_WARN,
                "help: buffer full at command '%s'", name);
            break;
        }
        used += written;
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "help: displayed %zu bytes", used);
    METRICS_CMD(help);

    return reply_markdown(ctx, buffer);
}
