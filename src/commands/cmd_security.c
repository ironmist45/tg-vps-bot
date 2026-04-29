/**
 * tg-bot - Telegram bot for system administration
 * cmd_security.c - /fail2ban command handler (V2)
 * MIT License - Copyright (c) 2026
 */

#include "cmd_security.h"
#include "logger.h"
#include "exec.h"
#include "utils.h"
#include "security.h"
#include "reply.h"

#include <stdio.h>
#include <string.h>

// ============================================================================
// /fail2ban COMMAND (V2)
// ============================================================================
/**
 * Manage Fail2Ban via f2b-wrapper
 * 
 * Subcommands:
 *   /fail2ban status [jail]  - Show ban status (optionally for specific jail)
 *   /fail2ban ban <ip>       - Ban IP address in sshd jail
 *   /fail2ban unban <ip>     - Unban IP address from sshd jail
 * 
 * Output is displayed in plain text (code block for status).
 * IP addresses are validated before passing to wrapper.
 * 
 * @param ctx  Command context
 * @return     0 on success, -1 on error
 */

int cmd_fail2ban_v2(command_ctx_t *ctx)
{
    // No arguments: show usage
    if (!ctx->args || ctx->args[0] == '\0') {
        return reply_markdown(ctx,
            "🛡 *Fail2Ban*\n\n"
            "`/fail2ban status`\n"
            "`/fail2ban status sshd`\n"
            "`/fail2ban ban <ip>`\n"
            "`/fail2ban unban <ip>`");
    }

    // Parse subcommand and arguments
    char subcmd[32] = {0};
    char arg[64] = {0};
    
    int parsed = sscanf(ctx->args, "%31s %63s", subcmd, arg);
    
    if (parsed < 1) {
        return reply_error(ctx, "Invalid command");
    }

    // ------------------------------------------------------------------------
    // STATUS subcommand
    // ------------------------------------------------------------------------
    if (strcmp(subcmd, "status") == 0) {
        char tmp[RESP_MAX];
        exec_result_t res;
        int rc;

        // Dynamically build args based on whether jail is specified
        if (arg[0] != '\0') {
            char *const args[] = {
                "sudo", "-n",
                "/usr/local/bin/f2b-wrapper",
                "status",
                arg,
                NULL
            };
            LOG_CMD_CTX(ctx, LOG_INFO, "fail2ban args: [0]=%s [1]=%s [2]=%s [3]=%s [4]=%s [5]=%s",
                args[0], args[1], args[2], args[3], args[4] ? args[4] : "NULL", args[5] ? args[5] : "NULL"); // debug cmdline
            rc = exec_command(args, tmp, sizeof(tmp), NULL, &res);
        } else {
            char *const args[] = {
                "sudo", "-n",
                "/usr/local/bin/f2b-wrapper",
                "status",
                NULL
            };
            LOG_CMD_CTX(ctx, LOG_INFO, "fail2ban args: [0]=%s [1]=%s [2]=%s [3]=%s [4]=%s [5]=%s",
                args[0], args[1], args[2], args[3], args[4] ? args[4] : "NULL", args[5] ? args[5] : "NULL"); // debug cmdline
            rc = exec_command(args, tmp, sizeof(tmp), NULL, &res);
        }

        if (rc != 0) {
            if (res.status == EXEC_TIMEOUT) {
                return reply_error(ctx, "Fail2Ban timeout");
            } else if (res.status == EXEC_EXEC_FAILED) {
                return reply_error(ctx, "Fail2Ban binary error\nPossible GLIBC mismatch");
            } else {
                return reply_error(ctx, "Fail2Ban failed");
            }
        }

        LOG_CMD_CTX(ctx, LOG_INFO, "fail2ban status: %s", arg[0] ? arg : "all");
        safe_code_block(tmp, ctx->response, ctx->resp_size);
        
        if (ctx->resp_type) {
            *(ctx->resp_type) = RESP_PLAIN;
        }
        return 0;
    }

    // ------------------------------------------------------------------------
    // BAN subcommand
    // ------------------------------------------------------------------------
    if (strcmp(subcmd, "ban") == 0) {
        if (arg[0] == '\0') {
            return reply_error(ctx, "Usage: /fail2ban ban <ip>");
        }

        if (!is_safe_ip(arg)) {
            LOG_CMD_CTX(ctx, LOG_WARN, "fail2ban ban: invalid IP %s", arg);
            return reply_error(ctx, "Invalid IP address");
        }

        char *const args[] = {
            "sudo", "-n",
            "/usr/local/bin/f2b-wrapper",
            "set",
            "sshd",
            "banip",
            arg,
            NULL
        };

        char tmp[RESP_MAX];
        exec_result_t res;

        if (exec_command(args, tmp, sizeof(tmp), NULL, &res) != 0) {
            return reply_error(ctx, "Ban failed");
        }

        if (res.status == EXEC_EXIT_NONZERO) {
            safe_code_block(tmp, ctx->response, ctx->resp_size);
            if (ctx->resp_type) *(ctx->resp_type) = RESP_PLAIN;
            return -1;
        }

        LOG_CMD_CTX(ctx, LOG_INFO, "fail2ban ban: %s", arg);
        return reply_ok(ctx, "IP banned");
    }

    // ------------------------------------------------------------------------
    // UNBAN subcommand
    // ------------------------------------------------------------------------
    if (strcmp(subcmd, "unban") == 0) {
        if (arg[0] == '\0') {
            return reply_error(ctx, "Usage: /fail2ban unban <ip>");
        }

        if (!is_safe_ip(arg)) {
            LOG_CMD_CTX(ctx, LOG_WARN, "fail2ban unban: invalid IP %s", arg);
            return reply_error(ctx, "Invalid IP address");
        }

        char *const args[] = {
            "sudo", "-n",
            "/usr/local/bin/f2b-wrapper",
            "set",
            "sshd",
            "unbanip",
            arg,
            NULL
        };

        char tmp[RESP_MAX];
        exec_result_t res;

        if (exec_command(args, tmp, sizeof(tmp), NULL, &res) != 0) {
            return reply_error(ctx, "Unban failed");
        }

        if (res.status == EXEC_EXIT_NONZERO) {
            safe_code_block(tmp, ctx->response, ctx->resp_size);
            if (ctx->resp_type) *(ctx->resp_type) = RESP_PLAIN;
            return -1;
        }

        LOG_CMD_CTX(ctx, LOG_INFO, "fail2ban unban: %s", arg);
        return reply_ok(ctx, "IP unbanned");
    }

    // ------------------------------------------------------------------------
    // Unknown subcommand
    // ------------------------------------------------------------------------
    LOG_CMD_CTX(ctx, LOG_WARN, "fail2ban unknown subcommand: %s", subcmd);
    return reply_error(ctx, "Unknown fail2ban command");
}
