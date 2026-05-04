/**
 * tg-bot - Telegram bot for system administration
 *
 * commands.c - Command dispatcher and routing logic
 *
 * Routes incoming Telegram messages to command handlers (V2 style).
 *
 * Command table is defined statically; actual implementations are in:
 *   - cmd_system.c   (/status, /health, /about, /ping)
 *   - cmd_services.c (/services, /users, /logs)
 *   - cmd_help.c     (/help)
 *   - cmd_security.c (/fail2ban)
 *   - cmd_control.c  (/start, /reboot, /restart, /totp_setup)
 *
 * Two-step confirmation flow:
 *   Commands with requires_confirmation = 1 are intercepted before
 *   the handler is called. The dispatcher stores a pending_confirm_t
 *   slot and asks the user for /confirm <code>.
 *
 *   On /confirm:
 *     - If g_cfg.totp_secret is set  → validate via TOTP (RFC 6238)
 *     - If g_cfg.totp_secret is empty → validate via classic stateless token
 *
 * MIT License
 *
 * Copyright (c) 2026 ironmist45
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "commands.h"
#include "config.h"
#include "lifecycle.h"
#include "logger.h"
#include "reply.h"
#include "security.h"
#include "telegram.h"
#include "totp.h"
#include "utils.h"

#include "cmd_system.h"
#include "cmd_services.h"
#include "cmd_help.h"
#include "cmd_security.h"
#include "cmd_control.h"
#include "metrics.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>

/* Maximum number of arguments per command (including command name) */
#define MAX_ARGS 8

/* Process start time (defined in lifecycle.c, used by /ping command) */
extern time_t g_start_time;

// ============================================================================
// PENDING CONFIRMATION STATE (single slot — single-user bot)
// ============================================================================

/*
 * At most one command can be awaiting confirmation at any time.
 * The slot is set when a command with requires_confirmation = 1 is received
 * and cleared when /confirm <code> is processed (valid or expired).
 */
static pending_confirm_t g_pending = {0};

/*
 * Reset the pending slot — called after successful confirmation,
 * failed validation, or expiry.
 */
static void pending_clear(void) {
    memset(&g_pending, 0, sizeof(g_pending));
}

/*
 * Check whether the pending slot has expired.
 * Returns 1 if expired (or inactive), 0 if still valid.
 */
static int pending_expired(void) {
    if (!g_pending.active)
        return 1;
    return (time(NULL) > g_pending.expires_at) ? 1 : 0;
}

// ============================================================================
// COMMAND TABLE
// ============================================================================

/**
 * Static command registry.
 *
 * Fields:
 *   name                  — command string (e.g. "/status")
 *   handler_v2            — V2 handler (command_ctx_t style)
 *   description           — shown in /help (NULL = hidden)
 *   category              — grouping for /help (NULL = hidden)
 *   requires_confirmation — 1 = needs /confirm before execution
 *                           Commands with this flag show 🔐 in /help
 */
command_t commands[] = {
    /* General */
    {"/start",    cmd_start_v2, NULL,             "General", 0},
    {"/help",     cmd_help_v2,  "Show this help", "General", 0},

    /* System status */
    {"/status",   cmd_status_v2, "System status", "System",      0},
    {"/health",   cmd_health_v2, "Health check",  "System",      0},
    {"/about",    cmd_about_v2,  "About bot",      "System info", 0},
    {"/ping",     cmd_ping_v2,   NULL,             "System info", 0},

    /* Service management */
    {"/services", cmd_services_v2, NULL, "Services", 0},
    {"/users",    cmd_users_v2,    NULL, "Services", 0},
    {"/logs",     cmd_logs_v2,     NULL, "Services", 0},

    /* Security */
    {"/fail2ban", cmd_fail2ban_v2, "Manage Fail2Ban", "Security", 0},
    {"/totp_setup", cmd_totp_setup_v2, "TOTP 2FA setup", "Security", 0},

    /*
     * System control — requires two-step confirmation.
     *
     * requires_confirmation = 1:
     *   First call  → dispatcher stores pending slot, sends confirmation request
     *   /confirm    → validates code (TOTP or classic token), executes handler
     *
     * /reboot_confirm and /restart_confirm are kept for backward compatibility
     * but hidden from /help. After full TOTP migration they will be removed.
     */
    {"/reboot",          cmd_reboot_v2,          "Reboot system", "System", 1},
    {"/reboot_confirm",  cmd_reboot_confirm_v2,  NULL,            NULL,     0},  /* hidden, legacy */
    {"/restart",         cmd_restart_v2,         "Restart bot",   "System", 1},
    {"/restart_confirm", cmd_restart_confirm_v2, NULL,            NULL,     0},  /* hidden, legacy */

    /* Universal confirmation handler — hidden from /help */
    {"/confirm", NULL, NULL, NULL, 0},  /* handled inline in dispatcher */
};

const int commands_count = sizeof(commands) / sizeof(commands[0]);

// ============================================================================
// CONFIRMATION REQUEST HELPERS
// ============================================================================

/*
 * Send a confirmation request to the user.
 *
 * If TOTP is configured: ask for the code from the authenticator app.
 * If not:               generate a classic token and send it.
 *
 * Stores the pending slot so /confirm knows what to execute.
 */
static int request_confirmation(command_ctx_t *ctx, const char *cmd_name) {
    int ttl = security_get_token_ttl();

    g_pending.active     = 1;
    g_pending.chat_id    = ctx->chat_id;
    g_pending.expires_at = time(NULL) + ttl;
    strncpy(g_pending.command, cmd_name, sizeof(g_pending.command) - 1);
    g_pending.command[sizeof(g_pending.command) - 1] = '\0';

    if (g_cfg.totp_secret[0] != '\0') {
        /* TOTP flow */
        LOG_SEC(LOG_INFO, "req=%04x confirm: TOTP requested for %s",
                ctx->req_id, cmd_name);

        snprintf(ctx->response, ctx->resp_size,
            "🔐 *Confirm:* `%s`\n\n"
            "Enter the code from your authenticator app:\n"
            "`/confirm <code>`\n\n"
            "_Valid for %d seconds_",
            cmd_name, ttl);
    } else {
        /* Classic token flow (fallback) */
        int token = security_generate_reboot_token(ctx->chat_id, ctx->req_id);
        g_pending.token = token;

        LOG_SEC(LOG_INFO, "req=%04x confirm: token=%06d requested for %s",
                ctx->req_id, token, cmd_name);

        snprintf(ctx->response, ctx->resp_size,
            "⚠️ *Confirm:* `%s`\n\n"
            "`/confirm %d`\n\n"
            "_Token valid for %d seconds_",
            cmd_name, token, ttl);
    }

    *ctx->resp_type = RESP_MARKDOWN;
    return 0;
}

/*
 * Handle /confirm <code>.
 *
 * Validates the code against TOTP or classic token depending on config.
 * On success: dispatches the pending command handler.
 * On failure: clears pending slot and returns an error message.
 */
static int handle_confirm(command_ctx_t *ctx) {
    /* Must have a code argument */
    if (!ctx->args || ctx->args[0] == '\0') {
        snprintf(ctx->response, ctx->resp_size,
            "Usage: `/confirm <code>`");
        *ctx->resp_type = RESP_MARKDOWN;
        return -1;
    }

    /* Check pending slot exists and has not expired */
    if (!g_pending.active || pending_expired()) {
        pending_clear();
        LOG_SEC(LOG_WARN, "req=%04x confirm: no pending command or expired",
                ctx->req_id);
        snprintf(ctx->response, ctx->resp_size,
            "⚠️ No pending command or token expired\\.\nPlease retry the command\\.");
        *ctx->resp_type = RESP_MARKDOWN;
        return -1;
    }

    /* Parse the code */
    int code;
    if (parse_int_nonneg(ctx->args, &code) != 0) {
        LOG_SEC(LOG_WARN, "req=%04x confirm: invalid code format '%s'",
                ctx->req_id, ctx->args);
        return reply_plain(ctx, "❌ Invalid code format");
    }

    /* Validate — TOTP or classic token */
    int valid = 0;

    if (g_cfg.totp_secret[0] != '\0') {
        /* TOTP validation */
        valid = (totp_verify(g_cfg.totp_secret, code) == 0) ? 1 : 0;

        if (!valid) {
            LOG_SEC(LOG_WARN, "req=%04x confirm: invalid TOTP code for %s",
                    ctx->req_id, g_pending.command);
            pending_clear();
            return reply_plain(ctx, "❌ Invalid or expired TOTP code");
        }
    } else {
        /* Classic token validation */
        valid = (security_validate_reboot_token(
                    ctx->chat_id, code, ctx->req_id) == 0) ? 1 : 0;

        if (!valid) {
            LOG_SEC(LOG_WARN, "req=%04x confirm: invalid token for %s",
                    ctx->req_id, g_pending.command);
            pending_clear();
            return reply_plain(ctx, "❌ Invalid or expired token");
        }
    }

    /* Code accepted — find and execute the pending command handler */
    LOG_SEC(LOG_INFO, "req=%04x confirm: accepted for %s",
            ctx->req_id, g_pending.command);

    char confirmed_cmd[32];
    strncpy(confirmed_cmd, g_pending.command,
            sizeof(confirmed_cmd) - 1);
    confirmed_cmd[sizeof(confirmed_cmd) - 1] = '\0';

    pending_clear();  /* Clear before executing to prevent replay */

    for (int i = 0; i < commands_count; i++) {
        if (strcmp(confirmed_cmd, commands[i].name) == 0) {
            if (commands[i].handler_v2) {
                return commands[i].handler_v2(ctx);
            }
        }
    }

    LOG_SEC(LOG_ERROR, "req=%04x confirm: handler not found for %s",
            ctx->req_id, confirmed_cmd);
    snprintf(ctx->response, ctx->resp_size, "Internal error");
    return -1;
}

// ============================================================================
// COMMAND DISPATCHER
// ============================================================================

int commands_handle(const char *text,
                    long chat_id,
                    time_t msg_date,
                    int user_id,
                    const char *username,
                    unsigned short req_id,
                    char *response,
                    size_t resp_size,
                    response_type_t *resp_type)
{
    response_type_t local_resp_type = RESP_MARKDOWN;

    /* Basic validation */
    if (!text || text[0] != '/') {
        snprintf(response, resp_size, "Invalid command");
        return -1;
    }

    if (security_validate_text(text) != 0) {
        LOG_SEC(LOG_WARN, "Rejected invalid input");
        snprintf(response, resp_size, "Invalid input");
        return -1;
    }

    if (security_rate_limit() != 0) {
        LOG_SEC(LOG_WARN, "Rate limit exceeded (chat_id=%ld)", chat_id);
        snprintf(response, resp_size, "Too many requests");
        return -1;
    }

    /* Parse command and arguments */
    char buffer[512];
    if (safe_copy(buffer, sizeof(buffer), text) != 0) {
        snprintf(response, resp_size, "Command too long");
        return -1;
    }

    char *argv[MAX_ARGS];
    int argc = split_args(buffer, argv, MAX_ARGS);

    if (argc > MAX_ARGS) {
        LOG_SEC(LOG_WARN, "Too many arguments");
        snprintf(response, resp_size, "Too many arguments");
        return -1;
    }

    if (argc == 0) {
        snprintf(response, resp_size, "Empty command");
        return -1;
    }

    /* Access control */
    if (security_check_access(chat_id, argv[0], req_id) != 0) {
        snprintf(response, resp_size, "Access denied");
        return -1;
    }

    /* Build args string */
    char args_buffer[512] = {0};
    if (argc > 1) {
        for (int j = 1; j < argc; j++) {
            if (j > 1) strncat(args_buffer, " ",
                               sizeof(args_buffer) - strlen(args_buffer) - 1);
            strncat(args_buffer, argv[j],
                    sizeof(args_buffer) - strlen(args_buffer) - 1);
        }
    }

    /* Build context */
    command_ctx_t ctx = {
        .chat_id   = chat_id,
        .user_id   = user_id,
        .username  = username,
        .args      = (args_buffer[0] != '\0') ? args_buffer : NULL,
        .raw_text  = text,
        .msg_date  = msg_date,
        .response  = response,
        .resp_size = resp_size,
        .resp_type = &local_resp_type,
        .req_id    = req_id
    };

    /* -----------------------------------------------------------------------
     * /confirm — universal confirmation handler
     * Handled inline before the main command loop.
     * ----------------------------------------------------------------------- */
    if (strcmp(argv[0], "/confirm") == 0) {
        int rc = handle_confirm(&ctx);
        if (resp_type) *resp_type = local_resp_type;
        return rc;
    }

    /* -----------------------------------------------------------------------
     * Main command dispatch
     * ----------------------------------------------------------------------- */
    for (int i = 0; i < commands_count; i++) {
        if (strcmp(argv[0], commands[i].name) != 0)
            continue;

        if (!commands[i].handler_v2)
            break;

        LOG_NET_CTX(&ctx, LOG_INFO, "cmd: %s [v2]", argv[0]);

        /* -------------------------------------------------------------------
         * Two-step confirmation gate
         * ------------------------------------------------------------------- */
        if (commands[i].requires_confirmation) {
            /*
             * If there is already an active pending slot for this chat_id
             * and the same command — the user sent the command twice without
             * confirming. Overwrite the slot (refreshes the expiry).
             */
            if (g_pending.active && !pending_expired()
                && g_pending.chat_id == chat_id
                && strcmp(g_pending.command, argv[0]) == 0) {
                LOG_SEC(LOG_INFO,
                    "req=%04x confirm: re-requesting for %s",
                    req_id, argv[0]);
            }

            int rc = request_confirmation(&ctx, argv[0]);
            if (resp_type) *resp_type = local_resp_type;
            return rc;
        }

        /* -------------------------------------------------------------------
         * Direct execution (no confirmation required)
         * ------------------------------------------------------------------- */
        int rc = commands[i].handler_v2(&ctx);

        LOG_NET_CTX(&ctx, LOG_DEBUG,
                    "handler done rc=%d resp_len=%zu",
                    rc, strlen(response));

        if (response[0] == '\0')
            snprintf(response, resp_size, (rc == 0) ? "OK" : "Error");

        if (resp_type) *resp_type = local_resp_type;
        return rc;
    }

    /* Unknown command */
    METRICS_CMD(other);
    LOG_CMD(LOG_WARN, "UNKNOWN CMD %.32s (chat_id=%ld)", argv[0], chat_id);
    snprintf(response, resp_size, "Unknown command");
    return -1;
}
