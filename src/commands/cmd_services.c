/* tg-bot — cmd_services.c — Service-related command handlers (/services, /users, /logs, /service). MIT License © 2026 ironmist45 */

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

    if (services_get_status(buffer, sizeof(buffer), ctx->req_id) != 0)
        return reply_error(ctx, "Failed to get services");

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

    if (users_get(buffer, sizeof(buffer), ctx->req_id) != 0)
        return reply_error(ctx, "Failed to get users");

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

        LOG_CMD_CTX(ctx, LOG_INFO, "service: showed usage menu");
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

// ============================================================================
// /service COMMAND (V2)
// ============================================================================

/**
 * Perform an action on a single whitelisted service.
 *
 * Usage:
 *   /service <alias> <action>
 *
 * Aliases:  ssh, shadowsocks, mtg (as defined in services_config.c)
 * Actions:  status, start, stop, restart
 *
 * status   — executes immediately, no confirmation required
 * start    — requests two-step confirmation via commands_request_confirm()
 * stop     — requests two-step confirmation via commands_request_confirm()
 * restart  — requests two-step confirmation via commands_request_confirm()
 *
 * Registered with requires_confirmation = 0 because only some actions need
 * confirmation. commands_request_confirm() is called directly for those.
 *
 * Confirmation flow:
 *   1. User sends: /service ssh restart
 *   2. Handler parses args, validates alias, calls commands_request_confirm()
 *      with cmd_name="/service" and args="ssh restart"
 *   3. Dispatcher saves "ssh restart" in g_pending.args, sends TOTP/token prompt
 *   4. User sends: /confirm <code>
 *   5. Dispatcher validates code, restores ctx->args = "ssh restart",
 *      calls cmd_service_v2() again
 *   6. Handler re-parses "ssh restart", executes service_action()
 *
 * @param ctx  Command context (ctx->args = "ssh restart" etc.)
 * @return     0 on success, error reply on failure
 */
int cmd_service_v2(command_ctx_t *ctx)
{
    // ------------------------------------------------------------------------
    // No arguments: show usage menu
    // ------------------------------------------------------------------------
    if (!ctx->args || ctx->args[0] == '\0') {
        char menu[512];
        size_t pos = 0;

        int n = snprintf(menu, sizeof(menu),
            "*⚙️ SERVICE*\n\n"
            "Usage: `/service <alias> <action>`\n\n"
            "*Actions:* `status` `start` `stop` `restart`\n\n"
            "*Available services:*\n");
        if (n > 0 && (size_t)n < sizeof(menu)) pos += (size_t)n;

        for (int i = 0; i < g_services_count; i++) {
            n = snprintf(menu + pos, sizeof(menu) - pos,
                         "  `%s`\n", g_services[i].alias);
            if (n < 0 || pos + (size_t)n >= sizeof(menu)) break;
            pos += (size_t)n;
        }
        menu[pos] = '\0';

        return reply_markdown(ctx, menu);
    }

    // ------------------------------------------------------------------------
    // Parse alias and action ("ssh restart" → alias="ssh" action="restart")
    // ------------------------------------------------------------------------
    char alias[32]      = {0};
    char action_str[32] = {0};

    if (sscanf(ctx->args, "%31s %31s", alias, action_str) < 2) {
        LOG_CMD_CTX(ctx, LOG_WARN, "service: bad args '%s'", ctx->args);
        return reply_markdown(ctx,
            "Usage: `/service <alias> <action>`\n"
            "Actions: `status` `start` `stop` `restart`");
    }

    // ------------------------------------------------------------------------
    // Resolve action string to enum
    // ------------------------------------------------------------------------
    service_action_t action;

    if      (strcmp(action_str, "status")  == 0) action = SERVICE_ACTION_STATUS;
    else if (strcmp(action_str, "start")   == 0) action = SERVICE_ACTION_START;
    else if (strcmp(action_str, "stop")    == 0) action = SERVICE_ACTION_STOP;
    else if (strcmp(action_str, "restart") == 0) action = SERVICE_ACTION_RESTART;
    else {
        LOG_CMD_CTX(ctx, LOG_WARN, "service: unknown action '%s'", action_str);
        snprintf(ctx->response, ctx->resp_size,
            "❌ Unknown action: `%s`\n"
            "Use: `status`, `start`, `stop`, `restart`", action_str);
        *ctx->resp_type = RESP_MARKDOWN;
        return -1;
    }

    // ------------------------------------------------------------------------
    // STATUS — execute immediately, no confirmation needed
    // ------------------------------------------------------------------------
    if (action == SERVICE_ACTION_STATUS) {
        char out[128] = {0};
        int rc = service_action(alias, action, out, sizeof(out), ctx->req_id);

        LOG_CMD_CTX(ctx, LOG_INFO, "service status: alias=%s result='%s'",
                    alias, out);
        METRICS_CMD(service);

        if (rc != 0)
            return reply_markdown(ctx, out[0] ? out : "❌ Failed to get status");

        snprintf(ctx->response, ctx->resp_size, "⚙️ *SERVICE STATUS*\n\n%s", out);
        *ctx->resp_type = RESP_MARKDOWN;
        return 0;
    }

    // ------------------------------------------------------------------------
    // START / STOP / RESTART
    //
    // Validate alias before asking for confirmation — fail fast so the user
    // gets an error immediately rather than after entering the TOTP code.
    // ------------------------------------------------------------------------
    int found = 0;
    for (int i = 0; i < g_services_count; i++) {
        if (strcmp(g_services[i].alias, alias) == 0) { found = 1; break; }
    }

    if (!found) {
        LOG_CMD_CTX(ctx, LOG_WARN, "service: unknown alias '%s'", alias);
        snprintf(ctx->response, ctx->resp_size,
                 "❌ Unknown service: `%s`", alias);
        *ctx->resp_type = RESP_MARKDOWN;
        return -1;
    }

    /*
     * Request confirmation via the public dispatcher helper.
     * Saves ctx->args ("ssh restart") into g_pending.args so the dispatcher
     * can restore it into ctx->args when /confirm arrives, allowing this
     * handler to re-parse and execute the action on the second call.
     */
    LOG_CMD_CTX(ctx, LOG_INFO, "service: requesting confirmation for %s %s",
                action_str, alias);

    METRICS_CMD(service);
    return commands_request_confirm(ctx, "/service", ctx->args);
}
