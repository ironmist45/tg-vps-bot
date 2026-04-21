/**
 * tg-bot - Telegram bot for system administration
 * cmd_security.c - /fail2ban command handler
 * MIT License - Copyright (c) 2026
 */

#include "cmd_security.h"

#include "logger.h"
#include "exec.h"
#include "utils.h"
#include "security.h"

#include <stdio.h>
#include <string.h>

// ============================================================================
// /fail2ban COMMAND
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
 * @param argc       Argument count
 * @param argv       Argument vector
 * @param chat_id    Chat ID of requester (unused)
 * @param resp       Response buffer
 * @param size       Size of response buffer
 * @param resp_type  Output: response type (always RESP_PLAIN)
 * @return           0 on success, -1 on error
 */
int cmd_fail2ban(int argc, char *argv[],
                 long chat_id,
                 char *resp, size_t size,
                 response_type_t *resp_type) {

    if (resp_type) *resp_type = RESP_PLAIN;
    (void)chat_id;

    // Validate command array
    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    }

    // ------------------------------------------------------------------------
    // No subcommand: show usage
    // ------------------------------------------------------------------------
    if (argc < 2) {
        snprintf(resp, size,
            "🛡 Fail2Ban\n\n"
            "/fail2ban status\n"
            "/fail2ban status sshd\n"
            "/fail2ban ban <ip>\n"
            "/fail2ban unban <ip>");
        return 0;
    }

    // ------------------------------------------------------------------------
    // STATUS subcommand
    // ------------------------------------------------------------------------
    if (strcmp(argv[1], "status") == 0) {
        char *const args[] = {
            "sudo",
            "-n",
            "/usr/local/bin/f2b-wrapper",
            "status",
            (argc == 3) ? argv[2] : NULL,  // optional jail name
            NULL
        };

        char tmp[RESP_MAX];
        exec_result_t res;

        int rc = exec_command(args, tmp, sizeof(tmp), NULL, &res);

        if (rc != 0) {
            if (res.status == EXEC_TIMEOUT) {
                snprintf(resp, size, "❌ Fail2Ban timeout");
            } else if (res.status == EXEC_EXEC_FAILED) {
                snprintf(resp, size,
                    "❌ Fail2Ban binary error\nPossible GLIBC mismatch");
            } else {
                snprintf(resp, size,
                    "❌ Fail2Ban failed (%s)",
                    exec_status_str(res.status));
            }
            return 0;
        }

        safe_code_block(tmp, resp, size);
        return 0;
    }

    // ------------------------------------------------------------------------
    // BAN subcommand
    // ------------------------------------------------------------------------
    else if (strcmp(argv[1], "ban") == 0 && argc >= 3) {
        // Validate IP address format
        if (!is_safe_ip(argv[2])) {
            snprintf(resp, size, "Invalid IP");
            return -1;
        }

        char *const args[] = {
            "sudo",
            "-n",
            "/usr/local/bin/f2b-wrapper",
            "set",
            "sshd",
            "banip",
            argv[2],
            NULL
        };

        char tmp[RESP_MAX];
        exec_result_t res;

        if (exec_command(args, tmp, sizeof(tmp), NULL, &res) != 0) {
            snprintf(resp, size, "❌ Ban failed");
            return -1;
        }

        // Show wrapper output on non-zero exit
        if (res.status == EXEC_EXIT_NONZERO) {
            safe_code_block(tmp, resp, size);
            return -1;
        }

        snprintf(resp, size, "✅ IP banned");
        return 0;
    }

    // ------------------------------------------------------------------------
    // UNBAN subcommand
    // ------------------------------------------------------------------------
    else if (strcmp(argv[1], "unban") == 0 && argc >= 3) {
        // Validate IP address format
        if (!is_safe_ip(argv[2])) {
            snprintf(resp, size, "Invalid IP");
            return -1;
        }

        char *const args[] = {
            "sudo",
            "-n",
            "/usr/local/bin/f2b-wrapper",
            "set",
            "sshd",
            "unbanip",
            argv[2],
            NULL
        };

        char tmp[RESP_MAX];
        exec_result_t res;

        if (exec_command(args, tmp, sizeof(tmp), NULL, &res) != 0) {
            snprintf(resp, size, "❌ Unban failed");
            return -1;
        }

        // Show wrapper output on non-zero exit
        if (res.status == EXEC_EXIT_NONZERO) {
            safe_code_block(tmp, resp, size);
            return -1;
        }

        snprintf(resp, size, "✅ IP unbanned");
        return 0;
    }

    // ------------------------------------------------------------------------
    // Unknown subcommand
    // ------------------------------------------------------------------------
    else {
        snprintf(resp, size, "Invalid fail2ban command");
        return -1;
    }
}
