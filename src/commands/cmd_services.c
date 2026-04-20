#include "commands.h"
#include "reply.h"
#include "services.h"
#include "users.h"
#include "logs.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

// ==== COMMANDS: Services ====
// Bot commands:  /services,
// ============   /users, /logs

// ==== /services v2 command ====

int cmd_services_v2(command_ctx_t *ctx)
{
    char buffer[1024];

    if (services_get_status(buffer, sizeof(buffer)) != 0) {
        return reply_error(ctx, "Failed to get services");
    }

    return reply_markdown(ctx, buffer);
}

// ==== /users v2 command ====

int cmd_users_v2(command_ctx_t *ctx)
{
    char buffer[512];

    if (users_get(buffer, sizeof(buffer)) != 0) {
        return reply_error(ctx, "Failed to get users");
    }

    return reply_markdown(ctx, buffer);
}

// ==== /logs v2 command ====

int cmd_logs_v2(command_ctx_t *ctx)
{
    // ===== БЕЗ аргументов → меню =====
    if (!ctx->args || ctx->args[0] == '\0') {
        return reply_markdown(ctx,
            "*📜 LOGS MENU*\n\n"
            "`/logs ssh`\n"
            "`/logs mtg`\n"
            "`/logs shadowsocks`\n\n"
            "`/logs <service> <N>`\n"
            "`/logs <service> error`");
    }

    // ===== ДЛИНА =====
    if (strlen(ctx->args) >= 256) {
        return reply_error(ctx, "Too many arguments");
    }

    // ===== ВАЛИДАЦИЯ =====
    for (const char *p = ctx->args; *p; p++) {
        if (!isalnum((unsigned char)*p) &&
            *p != ' ' && *p != '_' && *p != '-') {
            return reply_error(ctx, "Invalid arguments");
        }
    }

    // ===== BACKEND =====
    char buffer[2048];

    if (logs_get(ctx->args, buffer, sizeof(buffer)) != 0) {
        return reply_error(ctx, "Failed to get logs");
    }

    return reply_plain(ctx, buffer);
}
