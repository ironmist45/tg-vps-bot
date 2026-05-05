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

/* Label buffer size for otpauth:// URI (e.g. "tg-bot:123456789") */
#define TOTP_LABEL_MAX 64

// ============================================================================
// INTERNAL: LEGACY CONFIRM FLOW
// ============================================================================

/**
 * Shared logic for legacy /reboot_confirm and /restart_confirm handlers.
 *
 * Validates a classic stateless token (not TOTP) and executes shutdown.
 * Kept permanently — legacy tokens remain supported alongside TOTP.
 *
 * @param ctx           Command context
 * @param shutdown_type SHUTDOWN_REBOOT or SHUTDOWN_RESTART
 * @param cmd_name      Command name for logging (e.g. "reboot")
 * @param reply_text    Markdown reply text on success
 * @return              0 on success, -1 on error
 */
static int legacy_confirm(command_ctx_t *ctx,
                           int shutdown_type,
                           const char *cmd_name,
                           const char *reply_text)
{
    if (!ctx->args || ctx->args[0] == '\0') {
        char usage[64];
        snprintf(usage, sizeof(usage), "Usage: /%s\\_confirm <token>", cmd_name);
        return reply_error(ctx, usage);
    }

    int token;
    if (parse_int(ctx->args, &token) != 0) {
        LOG_CMD_CTX(ctx, LOG_WARN, "%s_confirm: invalid token format '%s'",
                    cmd_name, ctx->args);
        return reply_error(ctx, "Invalid token format");
    }

    if (security_validate_reboot_token(ctx->chat_id, token, ctx->req_id) != 0) {
        LOG_CMD_CTX(ctx, LOG_WARN, "%s_confirm: invalid or expired token", cmd_name);
        return reply_error(ctx, "Invalid or expired token");
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "%s confirmed via legacy flow", cmd_name);

    if (shutdown_type == SHUTDOWN_REBOOT)
        METRICS_CMD(reboot);
    else
        METRICS_CMD(restart);

    lifecycle_request_shutdown(shutdown_type, ctx->chat_id);
    return reply_markdown(ctx, reply_text);
}

// ============================================================================
// /reboot COMMAND (V2)
// ============================================================================

/**
 * Execute system reboot after confirmation.
 *
 * Called by the dispatcher after /confirm <code> has been validated.
 * requires_confirmation = 1 in command table — dispatcher handles
 * the confirmation step (TOTP or legacy token).
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
 * Legacy two-step confirmation for /reboot.
 * Kept permanently — supports classic token flow alongside TOTP.
 */
int cmd_reboot_confirm_v2(command_ctx_t *ctx)
{
    return legacy_confirm(ctx, SHUTDOWN_REBOOT, "reboot",
                          "♻️ *Rebooting system\\.\\.\\.*");
}

// ============================================================================
// /restart COMMAND (V2)
// ============================================================================

/**
 * Execute bot restart after confirmation.
 *
 * Called by the dispatcher after /confirm <code> has been validated.
 * Only the bot process is restarted — the server itself is not affected.
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
 * Legacy two-step confirmation for /restart.
 * Kept permanently — supports classic token flow alongside TOTP.
 */
int cmd_restart_confirm_v2(command_ctx_t *ctx)
{
    return legacy_confirm(ctx, SHUTDOWN_RESTART, "restart",
                          "🔄 *Restarting bot\\.\\.\\.*");
}

// ============================================================================
// /totp_setup COMMAND (V2)
// ============================================================================

/**
 * Show TOTP setup information for Aegis / Google Authenticator.
 *
 * Three states:
 *
 *   1. TOTP_SECRET not configured:
 *      Shows setup instructions (generate secret, add to config,
 *      set TOTP_SETUP=enabled, reload, then /totp_setup again).
 *
 *   2. TOTP_SECRET configured, TOTP_SETUP=disabled (default):
 *      Returns "TOTP is configured and active. Setup is disabled."
 *      Safe default — prevents secret exposure after initial setup.
 *
 *   3. TOTP_SECRET configured, TOTP_SETUP=enabled:
 *      Shows the base32 secret and otpauth:// URI for import into
 *      Aegis, Google Authenticator, Authy or any RFC 6238 app.
 *      After adding to the app — set TOTP_SETUP=disabled.
 *
 * Does not require confirmation (circular dependency with TOTP).
 */
int cmd_totp_setup_v2(command_ctx_t *ctx)
{
    /* TOTP not configured — show setup instructions */
    if (g_cfg.totp_secret[0] == '\0') {
        return reply_plain(ctx,
            "🔐 TOTP Setup\n\n"
            "TOTP is not configured.\n\n"
            "To enable TOTP 2FA:\n"
            "1. Generate a secret:\n"
            "   python3 -c \"import base64, os; "
                "print(base64.b32encode(os.urandom(20)).decode())\"\n"
            "   or: openssl rand 20 | base32\n\n"
            "2. Add to config file:\n"
            "   TOTP_SECRET=YOUR_BASE32_SECRET_HERE\n"
            "   TOTP_SETUP=enabled\n\n"
            "3. Reload config:\n"
            "   kill -HUP $(pidof tg-bot)\n\n"
            "4. Send /totp_setup to get the import link.\n\n"
            "5. After adding to your app, set TOTP_SETUP=disabled\n"
            "   and reload config.");
    }

    /* TOTP configured but setup disabled — safe default */
    if (!g_cfg.totp_setup_enabled) {
        LOG_CMD_CTX(ctx, LOG_INFO,
            "totp_setup: disabled in config (TOTP is active)");
        return reply_plain(ctx,
            "🔐 TOTP is configured and active.\n\n"
            "Setup is disabled. To re-enable:\n"
            "   TOTP_SETUP=enabled in config file");
    }

    /* TOTP configured and setup enabled — show URI */
    char label[TOTP_LABEL_MAX];
    snprintf(label, sizeof(label), "tg-bot:%ld", ctx->chat_id);

    char uri[TOTP_URI_MAX];
    if (totp_uri(g_cfg.totp_secret, label, "tg-bot", uri, sizeof(uri)) != 0) {
        LOG_CMD_CTX(ctx, LOG_ERROR, "totp_setup: failed to generate URI");
        return reply_error(ctx, "Failed to generate TOTP URI");
    }

    LOG_CMD_CTX(ctx, LOG_INFO,
        "totp_setup: URI generated for chat_id=%ld", ctx->chat_id);

    /* TOTP_URI_MAX for URI + 256 for surrounding text */
    char msg[TOTP_URI_MAX + 256];
    snprintf(msg, sizeof(msg),
        "🔐 TOTP Setup\n\n"
        "Secret:\n"
        "%s\n\n"
        "Import link (Aegis / Google Authenticator):\n"
        "%s\n\n"
        "After adding to your app:\n"
        "Set TOTP_SETUP=disabled in config and reload.",
        g_cfg.totp_secret,
        uri);

    return reply_plain(ctx, msg);
}