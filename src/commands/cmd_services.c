#include "commands.h"
#include "services.h"
#include "users.h"
#include "logs.h"
#include "utils.h"

#include <stdio.h>

// ==== COMMANDS: Services ====
// Bot commands:  /services,
// ============   /users, /logs

// ==== /services v2 command ====

int cmd_services_v2(command_ctx_t *ctx)
{
    if (ctx->resp_type) {
        *(ctx->resp_type) = RESP_MARKDOWN;
    }

    if (services_get_status(ctx->response, ctx->resp_size) != 0) {
        snprintf(ctx->response, ctx->resp_size,
                 "⚠️ Failed to get services");
        return -1;
    }

    return 0;
}

// ==== /users v2 command ====

int cmd_users_v2(command_ctx_t *ctx)
{
    if (ctx->resp_type) {
        *(ctx->resp_type) = RESP_MARKDOWN;
    }

    if (users_get(ctx->response, ctx->resp_size) != 0) {
        snprintf(ctx->response, ctx->resp_size,
                 "⚠️ Failed to get users");
        return -1;
    }

    return 0;
}


// ==== /logs v2 command ====

int cmd_logs_v2(command_ctx_t *ctx)
{
    if (ctx->resp_type) {
        *(ctx->resp_type) = RESP_PLAIN;
    }

    // ===== БЕЗ аргументов → меню =====
    if (!ctx->args || ctx->args[0] == '\0') {
        snprintf(ctx->response, ctx->resp_size,
            "*📜 LOGS MENU*\n\n"
            "`/logs ssh`\n"
            "`/logs mtg`\n"
            "`/logs shadowsocks`\n\n"
            "`/logs <service> <N>`\n"
            "`/logs <service> error`");
        return 0;
    }

    // ===== КОПИРУЕМ args в буфер =====
    char args_buf[256];

    if (safe_copy(args_buf, sizeof(args_buf), ctx->args) != 0) {
        snprintf(ctx->response, ctx->resp_size, "Too many arguments");
        return -1;
    }

    // ===== вызываем старый backend =====
    if (logs_get(args_buf, ctx->response, ctx->resp_size) != 0) {
        snprintf(ctx->response, ctx->resp_size,
                 "⚠️ Failed to get logs");
        return -1;
    }

    return 0;
}
