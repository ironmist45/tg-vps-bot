/**
 * tg-bot - Telegram bot for system administration
 * 
 * commands.c - Command dispatcher and routing logic
 * 
 * Routes incoming Telegram messages to appropriate command handlers.
 * Supports both legacy (argc/argv) and V2 (command_ctx_t) handler styles.
 * 
 * Command table is defined statically; actual implementations are in:
 *   - cmd_system.c   (/status, /health, /about, /ping, /reboot)
 *   - cmd_services.c (/services, /users, /logs)
 *   - cmd_help.c     (/help)
 *   - cmd_security.c (/fail2ban)
 *   - cmd_control.c  (/start)
 * 
 * MIT License
 * 
 * Copyright (c) 2026
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
#include "lifecycle.h"
#include "logger.h"
#include "security.h"
#include "telegram.h"
#include "utils.h"

#include "cmd_system.h"
#include "cmd_services.h"
#include "cmd_help.h"
#include "cmd_security.h"
#include "cmd_control.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

// Maximum number of arguments per command (including command name)
#define MAX_ARGS 8

// Process start time (defined in lifecycle.c, used by /ping command)
extern time_t g_start_time;

// ============================================================================
// COMMAND TABLE
// ============================================================================

/**
 * Static command registry.
 * Each entry maps a command name to its handler(s) and metadata.
 * 
 * Fields:
 *   - name:        command string (e.g., "/status")
 *   - handler:     legacy handler (argc/argv style)
 *   - handler_v2:  modern handler (command_ctx_t style)
 *   - description: shown in /help (NULL = hidden)
 *   - category:    grouping for /help (NULL = hidden)
 */
command_t commands[] = {
    // General commands
    {"/start", NULL, cmd_start_v2, NULL, "General"},
    {"/help", NULL, cmd_help_v2, "Show this help", "General"},

    // System status commands
    {"/status", NULL, cmd_status_v2, "System status", "System"},
    {"/health", NULL, cmd_health_v2, "Health check", "System"},
    {"/about",  NULL, cmd_about_v2,  "About bot", "System info"},
    {"/ping",   NULL, cmd_ping_v2,   NULL, "System info"},

    // Service management commands
    {"/services", NULL, cmd_services_v2, NULL, "Services"},
    {"/users",    NULL, cmd_users_v2,    NULL, "Services"},
    {"/logs",     NULL, cmd_logs_v2,     NULL, "Services"},

    // Security commands
    {"/fail2ban", cmd_fail2ban, NULL, NULL, "Security"},

    // System control commands (require token confirmation)
    {"/reboot",         cmd_reboot,         NULL, NULL, "System"},
    {"/reboot_confirm", cmd_reboot_confirm, NULL, NULL, NULL},  // hidden
};

// Number of commands in the table
const int commands_count = sizeof(commands) / sizeof(commands[0]);

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
                    response_type_t *resp_type) {

    // Default response type (can be overridden by handlers)
    response_type_t local_resp_type = RESP_MARKDOWN;

    // ------------------------------------------------------------------------
    // Basic validation
    // ------------------------------------------------------------------------
    
    if (!text || text[0] != '/') {
        snprintf(response, resp_size, "Invalid command");
        return -1;
    }

    // Validate raw input for safety (no binary/control characters)
    if (security_validate_text(text) != 0) {
        LOG_SEC(LOG_WARN, "Rejected invalid input");
        snprintf(response, resp_size, "Invalid input");
        return -1;
    }

    // Rate limiting (prevents command spam)
    if (security_rate_limit() != 0) {
        LOG_SEC(LOG_WARN, "Rate limit exceeded (chat_id=%ld)", chat_id);
        snprintf(response, resp_size, "Too many requests");
        return -1;
    }

    // ------------------------------------------------------------------------
    // Parse command and arguments
    // ------------------------------------------------------------------------
    
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

    // ------------------------------------------------------------------------
    // Access control (single-user bot)
    // ------------------------------------------------------------------------
    
    if (security_check_access(chat_id, argv[0], req_id) != 0) {
        snprintf(response, resp_size, "Access denied");
        return -1;
    }
    
    // ------------------------------------------------------------------------
    // Find and execute command handler
    // ------------------------------------------------------------------------
    
    for (int i = 0; i < commands_count; i++) {
        if (strcmp(argv[0], commands[i].name) != 0) {
            continue;
        }

        // --------------------------------------------------------------------
        // V2 HANDLER (preferred, uses command_ctx_t)
        // --------------------------------------------------------------------
        if (commands[i].handler_v2) {
            command_ctx_t ctx = {
                .chat_id   = chat_id,
                .user_id   = user_id,
                .username  = username,
                .args      = (argc > 1) ? argv[1] : NULL,
                .raw_text  = text,
                .msg_date  = msg_date,
                .response  = response,
                .resp_size = resp_size,
                .resp_type = &local_resp_type,
                .req_id    = req_id
            };

            LOG_NET_CTX(&ctx, LOG_DEBUG, "dispatching command: %s", argv[0]);
            LOG_NET_CTX(&ctx, LOG_INFO, "cmd: %s [v2]", argv[0]);
            
            int rc = commands[i].handler_v2(&ctx);

            LOG_NET_CTX(&ctx, LOG_DEBUG,
                        "handler done rc=%d resp_len=%zu",
                        rc, strlen(response));

            // Fallback if handler didn't set a response
            if (response[0] == '\0') {
                snprintf(response, resp_size,
                         (rc == 0) ? "OK" : "Error");
            }

            if (resp_type) {
                *resp_type = local_resp_type;
            }

            return rc;
        }

        // --------------------------------------------------------------------
        // LEGACY HANDLER (argc/argv style)
        // --------------------------------------------------------------------
        if (!commands[i].handler) {
            snprintf(response, resp_size, "Command not implemented");
            return -1;
        }

        LOG_NET(LOG_INFO, "cmd: %.32s (chat_id=%ld)", argv[0], chat_id);

        int rc = commands[i].handler(
            argc, argv, chat_id, response, resp_size, &local_resp_type
        );

        // Fallback if handler didn't set a response
        if (response[0] == '\0') {
            snprintf(response, resp_size, (rc == 0) ? "OK" : "Error");
        }

        // --------------------------------------------------------------------
        // Build response preview for logging (strip markdown, normalize whitespace)
        // --------------------------------------------------------------------
        char preview[256];
        size_t j = 0;
        int last_was_sep = 1;
        int last_was_space = 0;

        for (size_t k = 0; response[k] && j < sizeof(preview) - 1; k++) {
            char c = response[k];
            
            // Skip UTF-8 continuation bytes (emojis break preview)
            if ((unsigned char)c >= 0x80) {
                continue;
            }

            // Convert newlines to " | " separator
            if (c == '\n' || c == '\r') {
                // Skip consecutive newlines
                while (response[k + 1] == '\n' || response[k + 1] == '\r') {
                    k++;
                }
                if (!last_was_sep && j > 0) {
                    preview[j++] = ' ';
                    if (j < sizeof(preview) - 1) preview[j++] = '|';
                    if (j < sizeof(preview) - 1) preview[j++] = ' ';
                }
                last_was_sep = 1;
                last_was_space = 0;
                continue;
            }

            // Strip markdown formatting characters
            if (c == '*' || c == '`' || c == '_') {
                continue;
            }

            // Remove space before slash (e.g., " /help" -> "/help")
            if (c == '/' && j > 0 && preview[j - 1] == ' ') {
                j--;
            }

            // Normalize multiple spaces
            if (c == ' ') {
                if (last_was_space || last_was_sep) {
                    continue;
                }
                last_was_space = 1;
            } else {
                last_was_space = 0;
                last_was_sep = 0;
            }

            preview[j++] = c;

            // Truncate long responses
            if (j >= 120) {
                if (j < sizeof(preview) - 4) {
                    preview[j++] = '.';
                    preview[j++] = '.';
                    preview[j++] = '.';
                }
                break;
            }
        }
        preview[j] = '\0';

        size_t full_len = strlen(response);

        // Log response preview (skip /logs command to avoid spam)
        if (strcmp(argv[0], "/logs") != 0) {
            log_msg(LOG_DEBUG,
                    "RES %s (%zu chars) → %s",
                    argv[0], full_len, preview);
        } else {
            log_msg(LOG_DEBUG,
                    "RES %s (%zu chars) → <skipped>",
                    argv[0], full_len);
        }

        if (resp_type) {
            *resp_type = local_resp_type;
        }

        return rc;
    }

    // ------------------------------------------------------------------------
    // Unknown command
    // ------------------------------------------------------------------------
    
    log_msg(LOG_WARN,
            "UNKNOWN CMD %.32s (chat_id=%ld)",
            argv[0], chat_id);

    snprintf(response, resp_size, "Unknown command");
    return -1;
}
