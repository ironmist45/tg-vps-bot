/* tg-bot — cmd_services.c — Service-related command handlers (/services, /users, /logs). MIT License © 2026 ironmist45 */

#include "commands.h"
#include "reply.h"
#include "services.h"
#include "services_config.h"
#include "users.h"
#include "logger.h"
#include "logs.h"
#include "utils.h"
#include "metrics.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

// ============================================================================
// /services COMMAND (V2)
// ============================================================================

/**
 * Display status of all whitelisted systemd services
 *
 * Output format: Markdown table with emoji indicators
 *   🟢 UP, 🔴 DOWN, 🟡 FAIL/STARTING, ⚪ UNKNOWN
 *
 * @param ctx  Command context
 * @return     0 on success, -1 on error
 */
int cmd_services_v2(command_ctx_t *ctx)
{
    char buffer[1024];

    if (services_get_status(buffer, sizeof(buffer), ctx->req_id) != 0) {
        return reply_error(ctx, "Failed to get services");
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "services: requested");
    METRICS_CMD(services);
    return reply_markdown(ctx, buffer);
}

// ============================================================================
// /users COMMAND (V2)
// ============================================================================

/**
 * Display currently logged-in users
 *
 * Output format: Markdown list with user details
 *   *👤 ACTIVE USERS: N*
 *
 *   • 👤 `username`
 *     🖥 `tty`
 *     🌐 host
 *     ⏱ login_time
 *
 * @param ctx  Command context
 * @return     0 on success, -1 on error
 */
int cmd_users_v2(command_ctx_t *ctx)
{
    char buffer[512];

    if (users_get(buffer, sizeof(buffer), ctx->req_id) != 0) {
        return reply_error(ctx, "Failed to get users");
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "users: requested");
    METRICS_CMD(users);
    return reply_markdown(ctx, buffer);
}

// ============================================================================
// /logs COMMAND (V2)
// ============================================================================

/**
 * View systemd journal logs with filtering
 *
 * Usage:
 *   /logs                       - Show available services menu
 *   /logs <service>             - Last 30 lines
 *   /logs <service> <N>         - Last N lines (max 200)
 *   /logs <service> <filter>    - Filter results (e.g., "error", "auth")
 *
 * Allowed services: aliases from services_config.c
 *
 * Input validation:
 *   - Arguments length < 256 chars
 *   - Allowed chars: alphanumeric, space, underscore, hyphen
 *
 * @param ctx  Command context
 * @return     0 on success, response type set to RESP_PLAIN
 */
int cmd_logs_v2(command_ctx_t *ctx)
{
    // ------------------------------------------------------------------------
    // No arguments: show available services menu (built from services_config)
    // ------------------------------------------------------------------------
    if (!ctx->args || ctx->args[0] == '\0') {
        char menu[512];
        size_t pos = 0;

        int n = snprintf(menu, sizeof(menu), "*📜 LOGS MENU*\n\n");
        if (n > 0 && (size_t)n < sizeof(menu))
            pos += (size_t)n;

        for (int i = 0; i < g_services_count; i++) {
            n = snprintf(menu + pos, sizeof(menu) - pos,
                         "`/logs %s`\n", g_services[i].alias);
            if (n < 0 || pos + (size_t)n >= sizeof(menu)) break;
            pos += (size_t)n;
        }

        n = snprintf(menu + pos, sizeof(menu) - pos,
                     "\n`/logs <service> <N>`\n"
                     "`/logs <service> error`");
        if (n > 0 && pos + (size_t)n < sizeof(menu))
            pos += (size_t)n;
        menu[pos] = '\0';

        return reply_markdown(ctx, menu);
    }

    // ------------------------------------------------------------------------
    // Length validation
    // ------------------------------------------------------------------------
    if (strlen(ctx->args) >= 256) {
        LOG_CMD_CTX(ctx, LOG_WARN, "logs: args too long '%s'", ctx->args);
        return reply_error(ctx, "Too many arguments");
    }

    // ------------------------------------------------------------------------
    // Character whitelist validation (prevent injection)
    // ------------------------------------------------------------------------
    for (const char *p = ctx->args; *p; p++) {
        if (!isalnum((unsigned char)*p) &&
            *p != ' ' && *p != '_' && *p != '-') {
            LOG_CMD_CTX(ctx, LOG_WARN, "logs: invalid args '%s'", ctx->args);
            return reply_error(ctx, "Invalid arguments");
        }
    }

    // ------------------------------------------------------------------------
    // Fetch and format logs
    // ------------------------------------------------------------------------
    if (logs_get(ctx->args, ctx->response, ctx->resp_size, ctx->req_id) != 0) {
        LOG_CMD_CTX(ctx, LOG_ERROR, "logs_get failed: args='%s'", ctx->args);
        return reply_error(ctx, "Failed to get logs");
    }

    /* Logs are returned as plain text (may contain raw journal output) */
    if (ctx->resp_type)
        *(ctx->resp_type) = RESP_PLAIN;

    METRICS_CMD(logs);
    return 0;
}
