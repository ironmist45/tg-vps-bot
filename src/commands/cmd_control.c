/**
 * tg-bot - Telegram bot for system administration
 * cmd_control.c - System control commands (/reboot, /reboot_confirm) (V2)
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "cmd_control.h"
#include "logger.h"
#include "security.h"
#include "lifecycle.h"
#include "reply.h"
#include "utils.h"
#include "metrics.h"

#include <stdio.h>
#include <string.h>

// ============================================================================
// /reboot COMMAND (V2)
// ============================================================================

int cmd_reboot_v2(command_ctx_t *ctx)
{
    int token = security_generate_reboot_token(ctx->chat_id, ctx->req_id);

    char msg[128];
    snprintf(msg, sizeof(msg),
        "⚠️ *Confirm reboot*\n\n"
        "`/reboot_confirm %d`\n\n"
        "Token valid for %d seconds",
        token, 60);  // TODO: get TTL from security config

    LOG_CMD_CTX(ctx, LOG_INFO, "reboot token generated: %06d", token);

    return reply_markdown(ctx, msg);
}

// ============================================================================
// /reboot_confirm COMMAND (V2)
// ============================================================================

int cmd_reboot_confirm_v2(command_ctx_t *ctx)
{
    if (!ctx->args || ctx->args[0] == '\0') {
        return reply_error(ctx, "Usage: /reboot_confirm <token>");
    }

    int token;
    if (parse_int(ctx->args, &token) != 0) {
        LOG_CMD_CTX(ctx, LOG_WARN, "reboot_confirm: invalid token format '%s'", ctx->args);
        return reply_error(ctx, "Invalid token format");
    }

    if (security_validate_reboot_token(ctx->chat_id, token, ctx->req_id) != 0) {
        LOG_CMD_CTX(ctx, LOG_WARN, "reboot_confirm: invalid or expired token");
        return reply_error(ctx, "Invalid or expired token");
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "reboot confirmed and executing");
    METRICS_CMD("reboot");

    // Request reboot via lifecycle module
    lifecycle_request_shutdown(SHUTDOWN_REBOOT, ctx->chat_id);

    return reply_markdown(ctx, "♻️ Rebooting system...");
}
