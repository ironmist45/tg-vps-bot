#include "commands.h"
#include "logger.h"
#include "security.h"
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

#define MAX_ARGS 8

extern time_t g_start_time;
extern volatile sig_atomic_t g_shutdown_requested;
extern long g_reboot_requested_by;

// ===== COMMANDS TABLE =====

command_t commands[] = {
    {"/start", cmd_start, NULL, NULL, "General"},
    {"/help", cmd_help, NULL, NULL, "General"},

    {"/status", cmd_status, NULL, "System status", "System info"},
    {"/status_mini", cmd_status_mini, NULL, "Short status", "System info"},
    {"/about", cmd_about, NULL, "About bot", "System info"},
    {"/ping", cmd_ping, NULL, NULL, "System info"},

    {"/services", cmd_services, NULL, NULL, "Services"},
    {"/users", cmd_users, NULL, NULL, "Services"},
    {"/logs", cmd_logs, NULL, NULL, "Services"},

    {"/fail2ban", cmd_fail2ban, NULL, NULL, "Security"},

    {"/reboot", cmd_reboot, NULL, NULL, "System"},
    {"/reboot_confirm", cmd_reboot_confirm, NULL, NULL, NULL},
};

const int commands_count =
    sizeof(commands) / sizeof(commands[0]);

// ===== DISPATCHER =====

int commands_handle(const char *text,
                    long chat_id,
                    char *response,
                    size_t resp_size,
                    response_type_t *resp_type) {

    response_type_t local_resp_type = RESP_MARKDOWN;

    if (!text || text[0] != '/') {
        snprintf(response, resp_size, "Invalid command");
        return -1;
    }

    // 🆕 SECURITY: validate raw input
    if (security_validate_text(text) != 0) {
        LOG_SEC(LOG_WARN, "Rejected invalid input");
        snprintf(response, resp_size, "Invalid input");
        return -1;
    }

    // 🆕 SECURITY: rate limit
    if (security_rate_limit() != 0) {
        LOG_SEC(LOG_WARN, "Rate limit exceeded (chat_id=%ld)", chat_id);
        snprintf(response, resp_size, "Too many requests");
        return -1;
    }

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

    // 🔒 STRICT ACCESS CONTROL (single-user bot)
    if (security_check_access(chat_id, argv[0]) != 0) {
        snprintf(response, resp_size, "Access denied");
        return -1;
    }

    // ===== SUBCOMMAND: /status =====
  
    if (strcmp(argv[0], "/status") == 0) {

        if (argc >= 2 && strcmp(argv[1], "mini") == 0) {

            LOG_NET(LOG_INFO,
                    "cmd: /status mini (chat_id=%ld)",
                    chat_id);

            int rc = cmd_status_mini(
                argc, argv, chat_id,
                response, resp_size,
                &local_resp_type
            );

            if (response[0] == '\0') {
                snprintf(response, resp_size,
                         (rc == 0) ? "OK" : "Error");
            }

            if (resp_type) {
                *resp_type = local_resp_type;
            }

            return rc;
        }

        // 👉 ОБЫЧНЫЙ /status (был потерян!)
        LOG_NET(LOG_INFO,
                "cmd: /status (chat_id=%ld)",
                chat_id);

        int rc = cmd_status(
            argc, argv, chat_id,
            response, resp_size,
            &local_resp_type
        );

        if (response[0] == '\0') {
            snprintf(response, resp_size,
                     (rc == 0) ? "OK" : "Error");
        }

        if (resp_type) {
            *resp_type = local_resp_type;
        }

        return rc;
    }

    for (int i = 0; i < commands_count; i++) {
      
         if (strcmp(argv[0], commands[i].name) == 0) {

            LOG_NET(LOG_INFO,
                    "cmd: %.32s (chat_id=%ld)",
                    argv[0], chat_id);

            // ===== V2 handler =====
            if (commands[i].handler_v2) {

                command_ctx_t ctx = {
                .chat_id = chat_id,
                .user_id = 0,
                .username = NULL,
                .args = (argc > 1) ? argv[1] : NULL,
                .raw_text = text
            };

            LOG_NET(LOG_INFO,
                    "cmd: %.32s [v2] (chat_id=%ld)",
                    argv[0], chat_id);

            int rc = commands[i].handler_v2(&ctx);

            // ⚠️ временный fallback (пока нет reply_* системы)
            if (response[0] == '\0') {
                snprintf(response, resp_size,
                         (rc == 0) ? "OK" : "Error");
            }

            if (resp_type) {
                *resp_type = local_resp_type;
            }

            return rc;
        }

        // ===== LEGACY handler =====
        if (!commands[i].handler) {
            snprintf(response, resp_size,
                     "Command not implemented");
            return -1;
        }

        LOG_NET(LOG_INFO,
                "cmd: %.32s (chat_id=%ld)",
                argv[0], chat_id);

        int rc = commands[i].handler(
            argc, argv, chat_id, response, resp_size, &local_resp_type
        );
            // ===== fallback если пустой ответ =====
            if (response[0] == '\0') {
                snprintf(response, resp_size,
                         (rc == 0) ? "OK" : "Error");
            }

            // ===== PREVIEW BUILD =====
            char preview[256];
            size_t j = 0;

            int last_was_sep = 1;
            int last_was_space = 0;

            for (size_t k = 0; response[k] && j < sizeof(preview) - 1; k++) {

                char c = response[k];
                // 🔹 убрать UTF-8 (эмодзи → ломают preview)
                if ((unsigned char)c >= 0x80) {
                    continue;
                    }

                // переносы → |
                if (c == '\n' || c == '\r') {

                    while (response[k + 1] == '\n' ||
                           response[k + 1] == '\r') {
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

                // убрать markdown
                if (c == '*' || c == '`' || c == '_') {
                    continue;
                }

                // убрать пробел перед "/"
                if (c == '/' && j > 0 && preview[j - 1] == ' ') {
                    j--;
                }

                // нормализация пробелов
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

                // truncate
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

            // ===== LOG RESPONSE =====
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
    }

    log_msg(LOG_WARN,
        "UNKNOWN CMD %.32s (chat_id=%ld)",
        argv[0], chat_id);

snprintf(response, resp_size, "Unknown command");
return -1;
}
