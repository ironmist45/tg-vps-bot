/**
 * tg-bot - Telegram bot for system administration
 * cmd_control.c - System control commands (/reboot, /restart) (V2)
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

/**
 * Step 1 of 2: generate a confirmation token and send it to the user.
 *
 * The token is stateless (derived from chat_id + time + runtime salt)
 * and valid for TOKEN_TTL seconds (configurable in config file).
 * The user must send /reboot_confirm <token> to proceed.
 */
int cmd_reboot_v2(command_ctx_t *ctx)
{
    int token = security_generate_reboot_token(ctx->chat_id, ctx->req_id);
    int ttl   = security_get_token_ttl();

    char msg[128];
    snprintf(msg, sizeof(msg),
        "⚠️ *Confirm reboot*\n\n"
        "`/reboot_confirm %d`\n\n"
        "Token valid for %d seconds",
        token, ttl);

    LOG_CMD_CTX(ctx, LOG_INFO, "reboot token generated: %06d (ttl=%d)", token, ttl);

    return reply_markdown(ctx, msg);
}

// ============================================================================
// /reboot_confirm COMMAND (V2)
// ============================================================================

/**
 * Step 2 of 2: validate the token and execute system reboot.
 *
 * Rejects invalid, expired, or replayed tokens.
 * After 5 consecutive failures the token check is blocked for 30 seconds.
 * On success triggers reboot via lifecycle_request_shutdown(SHUTDOWN_REBOOT).
 */
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
    METRICS_CMD(reboot);

    lifecycle_request_shutdown(SHUTDOWN_REBOOT, ctx->chat_id);

    return reply_markdown(ctx, "♻️ Rebooting system...");
}

// ============================================================================
// /restart COMMAND (V2)
// ============================================================================

/**
 * Step 1 of 2: generate a confirmation token and send it to the user.
 *
 * Behaves identically to /reboot step 1 — same token mechanism,
 * same TTL. The difference is in step 2: only the bot process is
 * restarted (systemctl restart tg-bot), the server is not affected.
 */
int cmd_restart_v2(command_ctx_t *ctx)
{
    int token = security_generate_reboot_token(ctx->chat_id, ctx->req_id);
    int ttl   = security_get_token_ttl();

    char msg[128];
    snprintf(msg, sizeof(msg),
        "🔄 *Confirm bot restart*\n\n"
        "`/restart_confirm %d`\n\n"
        "Token valid for %d seconds",
        token, ttl);

    LOG_CMD_CTX(ctx, LOG_INFO, "restart token generated: %06d (ttl=%d)", token, ttl);

    return reply_markdown(ctx, msg);
}

// ============================================================================
// /restart_confirm COMMAND (V2)
// ============================================================================

/**
 * Step 2 of 2: validate the token and restart the bot process.
 *
 * Same token validation as /reboot_confirm.
 * On success triggers restart via lifecycle_request_shutdown(SHUTDOWN_RESTART)
 * which executes: execl(systemctl, "restart", "tg-bot").
 * The server itself is not rebooted.
 */
int cmd_restart_confirm_v2(command_ctx_t *ctx)
{
    if (!ctx->args || ctx->args[0] == '\0') {
        return reply_error(ctx, "Usage: /restart_confirm <token>");
    }

    int token;
    if (parse_int(ctx->args, &token) != 0) {
        LOG_CMD_CTX(ctx, LOG_WARN, "restart_confirm: invalid token format '%s'", ctx->args);
        return reply_error(ctx, "Invalid token format");
    }

    if (security_validate_reboot_token(ctx->chat_id, token, ctx->req_id) != 0) {
        LOG_CMD_CTX(ctx, LOG_WARN, "restart_confirm: invalid or expired token");
        return reply_error(ctx, "Invalid or expired token");
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "restart confirmed and executing");
    METRICS_CMD(restart);

    lifecycle_request_shutdown(SHUTDOWN_RESTART, ctx->chat_id);

    return reply_markdown(ctx, "🔄 Restarting bot...");
}
