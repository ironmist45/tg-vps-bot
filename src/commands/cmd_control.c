/**
 * tg-bot - Telegram bot for system administration
 * cmd_control.c - System control commands (/reboot, /restart, /totp_setup) (V2)
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "cmd_control.h"
#include "config.h"
#include "logger.h"
#include "security.h"
#include "lifecycle.h"
#include "reply.h"
#include "totp.h"
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
 *
 * Note: when requires_confirmation = 1 in the command table, this handler
 * is called AFTER the dispatcher has already validated /confirm <code>.
 * The token generation here is kept for the legacy /reboot_confirm flow.
 */
int cmd_reboot_v2(command_ctx_t *ctx)
{
    LOG_CMD_CTX(ctx, LOG_INFO, "reboot confirmed and executing");
    METRICS_CMD(reboot);

    lifecycle_request_shutdown(SHUTDOWN_REBOOT, ctx->chat_id);

    return reply_markdown(ctx, "♻️ *Rebooting system\\.\\.\\.*");
}

// ============================================================================
// /reboot_confirm COMMAND (V2) — legacy fallback
// ============================================================================

/**
 * Legacy two-step confirmation for /reboot (kept for backward compatibility).
 *
 * Used when the user sends /reboot_confirm <token> directly instead of
 * going through the new /confirm flow. Will be removed after full TOTP
 * migration is complete.
 */
int cmd_reboot_confirm_v2(command_ctx_t *ctx)
{
    if (!ctx->args || ctx->args[0] == '\0') {
        return reply_error(ctx, "Usage: /reboot\\_confirm <token>");
    }

    int token;
    if (parse_int(ctx->args, &token) != 0) {
        LOG_CMD_CTX(ctx, LOG_WARN, "reboot_confirm: invalid token format '%s'",
                    ctx->args);
        return reply_error(ctx, "Invalid token format");
    }

    if (security_validate_reboot_token(ctx->chat_id, token, ctx->req_id) != 0) {
        LOG_CMD_CTX(ctx, LOG_WARN, "reboot_confirm: invalid or expired token");
        return reply_error(ctx, "Invalid or expired token");
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "reboot confirmed via legacy flow");
    METRICS_CMD(reboot);

    lifecycle_request_shutdown(SHUTDOWN_REBOOT, ctx->chat_id);

    return reply_markdown(ctx, "♻️ *Rebooting system\\.\\.\\.*");
}

// ============================================================================
// /restart COMMAND (V2)
// ============================================================================

/**
 * Execute bot restart after confirmation.
 *
 * Called by the dispatcher after /confirm <code> has been validated.
 * Only the bot process is restarted (systemctl restart tg-bot) —
 * the server itself is not affected.
 */
int cmd_restart_v2(command_ctx_t *ctx)
{
    LOG_CMD_CTX(ctx, LOG_INFO, "restart confirmed and executing");
    METRICS_CMD(restart);

    lifecycle_request_shutdown(SHUTDOWN_RESTART, ctx->chat_id);

    return reply_markdown(ctx, "🔄 *Restarting bot\\.\\.\\.*");
}

// ============================================================================
// /restart_confirm COMMAND (V2) — legacy fallback
// ============================================================================

/**
 * Legacy two-step confirmation for /restart (kept for backward compatibility).
 *
 * Used when the user sends /restart_confirm <token> directly instead of
 * going through the new /confirm flow. Will be removed after full TOTP
 * migration is complete.
 */
int cmd_restart_confirm_v2(command_ctx_t *ctx)
{
    if (!ctx->args || ctx->args[0] == '\0') {
        return reply_error(ctx, "Usage: /restart\\_confirm <token>");
    }

    int token;
    if (parse_int(ctx->args, &token) != 0) {
        LOG_CMD_CTX(ctx, LOG_WARN, "restart_confirm: invalid token format '%s'",
                    ctx->args);
        return reply_error(ctx, "Invalid token format");
    }

    if (security_validate_reboot_token(ctx->chat_id, token, ctx->req_id) != 0) {
        LOG_CMD_CTX(ctx, LOG_WARN, "restart_confirm: invalid or expired token");
        return reply_error(ctx, "Invalid or expired token");
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "restart confirmed via legacy flow");
    METRICS_CMD(restart);

    lifecycle_request_shutdown(SHUTDOWN_RESTART, ctx->chat_id);

    return reply_markdown(ctx, "🔄 *Restarting bot\\.\\.\\.*");
}

// ============================================================================
// /totp_setup COMMAND (V2)
// ============================================================================

/**
 * Show TOTP setup information for Aegis / Google Authenticator.
 *
 * If TOTP_SECRET is set in the config:
 *   - Shows the base32 secret
 *   - Shows the otpauth:// URI ready to import into any RFC 6238 app
 *
 * If TOTP_SECRET is not configured:
 *   - Explains how to generate a secret and configure the bot
 *
 * Does not require confirmation (circular dependency).
 */
int cmd_totp_setup_v2(command_ctx_t *ctx)
{
    if (g_cfg.totp_secret[0] == '\0') {
        return reply_plain(ctx,
            "🔐 TOTP Setup\n\n"
            "TOTP is not configured.\n\n"
            "To enable TOTP 2FA:\n"
            "1. Generate a secret:\n"
            "   openssl rand -base32 20\n\n"
            "2. Add to config file:\n"
            "   TOTP_SECRET=YOUR_SECRET_HERE\n\n"
            "3. Reload config:\n"
            "   kill -HUP $(pidof tg-bot)\n\n"
            "4. Send /totp_setup again to get the import link.");
    }

    char label[64];
    snprintf(label, sizeof(label), "tg-bot:%ld", ctx->chat_id);

    char uri[TOTP_URI_MAX];
    if (totp_uri(g_cfg.totp_secret, label, "tg-bot", uri, sizeof(uri)) != 0) {
        LOG_CMD_CTX(ctx, LOG_ERROR, "totp_setup: failed to generate URI");
        return reply_error(ctx, "Failed to generate TOTP URI");
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "totp_setup: URI generated for chat_id=%ld",
                ctx->chat_id);

    char msg[TOTP_URI_MAX + 256];
    snprintf(msg, sizeof(msg),
        "🔐 TOTP Setup\n\n"
        "Secret:\n"
        "%s\n\n"
        "Import link (Aegis / Google Authenticator):\n"
        "%s\n\n"
        "Paste this link into your authenticator app,\n"
        "or scan a QR code generated from it.",
        g_cfg.totp_secret,
        uri);

    return reply_plain(ctx, msg);
}
