#include "commands.h"
#include "logger.h"
#include "system.h"
#include "services.h"
#include "users.h"
#include "logs.h"
#include "security.h"
#include "version.h"
#include "utils.h"
#include "exec.h"
#include "cmd_system.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>

#define MAX_ARGS 8
#define RESP_MAX 8192

extern time_t g_start_time;
extern volatile sig_atomic_t g_shutdown_requested;
extern long g_reboot_requested_by;

// ===== forward declarations =====

static int cmd_help(int, char **, long, char *, size_t, response_type_t *);
static int cmd_logs(int, char **, long, char *, size_t, response_type_t *);
static int cmd_users(int, char **, long, char *, size_t, response_type_t *);
static int cmd_fail2ban(int, char **, long, char *, size_t, response_type_t *);
static int cmd_reboot(int, char **, long, char *, size_t, response_type_t *);
static int cmd_reboot_confirm(int, char **, long, char *, size_t, response_type_t *);

// ===== COMMANDS =====
// NOTE: this code has been moved to /src/commands/cmd_system.c!
// ===== SERVICES =====
// NOTE: this code has been moved to /src/commands/cmd_services.c!

// ===== LOGS =====

static int cmd_logs(int argc, char *argv[],
                    long chat_id,
                    char *resp, size_t size,
                    response_type_t *resp_type) {

    if (resp_type) *resp_type = RESP_PLAIN;

    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    } 

    REQUIRE_ACCESS(chat_id, argv[0], resp, size);

    if (argc < 2) {
        snprintf(resp, size,
            "*📜 LOGS MENU*\n\n"
            "`/logs ssh`\n"
            "`/logs mtg`\n"
            "`/logs shadowsocks`\n\n"
            "`/logs <service> <N>`\n"
            "`/logs <service> error`");
        return 0;
    }

    char args[256] = {0};
    size_t used = 0;

    for (int i = 1; i < argc; i++) {
        int written = snprintf(args + used,
                               sizeof(args) - used,
                               "%s%s",
                               argv[i],
                               (i < argc - 1) ? " " : "");
        if (written < 0 || (size_t)written >= sizeof(args) - used) {
            snprintf(resp, size, "Too many arguments");
            return -1;
        }
      
        used += written;
    }

        if (used >= sizeof(args) - 1) {
            snprintf(resp, size, "Too many arguments");
            return -1;
        }

    // ✅ FIX /logs
    if (logs_get(args, resp, size) != 0) {
        snprintf(resp, size, "⚠️ Failed to get logs");
        return -1;
    }

    return 0;
}

// ===== USERS =====

static int cmd_users(int argc, char *argv[],
                     long chat_id,
                     char *resp, size_t size,
                     response_type_t *resp_type) {
    (void)argc;

    if (resp_type) *resp_type = RESP_MARKDOWN;

    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    }

    REQUIRE_ACCESS(chat_id, argv[0], resp, size);
  
    if (users_get(resp, size) != 0) {
        snprintf(resp, size, "⚠️ Failed to get users");
        return -1;
    }
  
return 0;
  
}

// ===== FAIL2BAN (🔥 FIXED + LOGGING) =====
static int cmd_fail2ban(int argc, char *argv[],
                        long chat_id,
                        char *resp, size_t size,
                        response_type_t *resp_type) {

    if (resp_type) *resp_type = RESP_PLAIN;

    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    }

    REQUIRE_ACCESS(chat_id, argv[0], resp, size);

    if (argc < 2) {
        snprintf(resp, size,
            "🛡 Fail2Ban\n\n"
            "/fail2ban status\n"
            "/fail2ban status sshd\n"
            "/fail2ban ban <ip>\n"
            "/fail2ban unban <ip>");
        return 0;
    }

    if (strcmp(argv[1], "status") == 0) {

        if (argc == 2) {
            char *const args[] = {
                "sudo",
                "-n",
                "/usr/local/bin/f2b-wrapper",
                "status",
                NULL
        };

        char tmp[RESP_MAX];
        exec_result_t res;

        int rc = exec_command(args, tmp, sizeof(tmp), NULL, &res);

        // 🔴 1. Ошибка выполнения (fork/pipe/timeout и т.д.)
        if (rc != 0) {

            if (res.status == EXEC_TIMEOUT) {
                snprintf(resp, size, "❌ Fail2Ban timeout");
            }
            else if (res.status == EXEC_EXEC_FAILED) {
                    snprintf(resp, size,
                    "❌ Fail2Ban binary error\n"
                    "Possible GLIBC mismatch");
            }
            else {
                snprintf(resp, size,
                    "❌ Fail2Ban failed (%s)",
                    exec_status_str(res.status));
            }

            return 0;
        }

        // 🟡 2. Команда выполнилась, но exit != 0
        if (res.status == EXEC_EXIT_NONZERO) {
            safe_code_block(tmp, resp, size);
            return 0;
        }

        // 🟢 3. Успех
        safe_code_block(tmp, resp, size);
        return 0;
                  
    }
          
    else if (argc == 3 && strcmp(argv[2], "sshd") == 0) {
        char *const args[] = {
            "sudo",
            "-n",
            "/usr/local/bin/f2b-wrapper",
            "status",
            argv[2],
            NULL
        };

        char tmp[RESP_MAX];
        exec_result_t res;

        int rc = exec_command(args, tmp, sizeof(tmp), NULL, &res);

        // 🔴 1. Ошибка выполнения (fork/pipe/timeout и т.д.)
        if (rc != 0) {

            if (res.status == EXEC_TIMEOUT) {
                snprintf(resp, size, "❌ Fail2Ban timeout");
            }
            else if (res.status == EXEC_EXEC_FAILED) {
                    snprintf(resp, size,
                    "❌ Fail2Ban binary error\n"
                    "Possible GLIBC mismatch");
            }
            else {
                snprintf(resp, size,
                    "❌ Fail2Ban failed (%s)",
                    exec_status_str(res.status));
            }

            return 0;
        }

        // 🟡 2. Команда выполнилась, но exit != 0
        if (res.status == EXEC_EXIT_NONZERO) {
            safe_code_block(tmp, resp, size);
            return 0;
        }

        // 🟢 3. Успех
        safe_code_block(tmp, resp, size);
        return 0;
      
    }
      
    else {
        snprintf(resp, size, "Invalid status usage");
        return -1;
    }
}

    else if (strcmp(argv[1], "ban") == 0 && argc >= 3) {

        if (!is_safe_ip(argv[2])) {
            snprintf(resp, size, "Invalid IP");
            return -1;
        }

        LOG_STATE(LOG_WARN,
            "FAIL2BAN BAN: ip=%s (chat_id=%ld)",
            argv[2], chat_id);

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

            if (res.status == EXEC_TIMEOUT) {
              snprintf(resp, size, "❌ Ban timeout");
            }
            else if (res.status == EXEC_EXEC_FAILED) {
                snprintf(resp, size, "❌ Fail2Ban wrapper broken");
            }
            else {
                snprintf(resp, size,
                    "❌ Ban failed (%s)",
                    exec_status_str(res.status));
            }

            return -1;
      }

      if (res.status == EXEC_EXIT_NONZERO) {
          safe_code_block(tmp, resp, size);
          return -1;
      }

      snprintf(resp, size, "✅ IP banned");
      return 0;
      
}

    else if (strcmp(argv[1], "unban") == 0 && argc >= 3) {

        if (!is_safe_ip(argv[2])) {
            snprintf(resp, size, "Invalid IP");
            return -1;
        }

        LOG_STATE(LOG_WARN,
            "FAIL2BAN UNBAN: ip=%s (chat_id=%ld)",
            argv[2], chat_id);

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

            if (res.status == EXEC_TIMEOUT) {
                snprintf(resp, size, "❌ Unban timeout");
            }
            else if (res.status == EXEC_EXEC_FAILED) {
                snprintf(resp, size, "❌ Fail2Ban wrapper broken");
            }
            else {
                snprintf(resp, size,
                    "❌ Unban failed (%s)",
                    exec_status_str(res.status));
            }

            return -1;
        }

        if (res.status == EXEC_EXIT_NONZERO) {
            safe_code_block(tmp, resp, size);
            return -1;
        }

        snprintf(resp, size, "✅ IP unbanned");
        return 0;
      
}

    else {
        snprintf(resp, size, "Invalid fail2ban command");
        return -1;
    }
  
}
// ===== REBOOT =====

static int cmd_reboot(int argc, char *argv[],
                      long chat_id,
                      char *resp, size_t size,
                      response_type_t *resp_type) {
    (void)argc;

    if (resp_type) *resp_type = RESP_MARKDOWN;
    
    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    }

    REQUIRE_ACCESS(chat_id, argv[0], resp, size);

    int token = security_generate_reboot_token(chat_id);

    int written = snprintf(resp, size,
        "⚠️ Confirm reboot:\n`/reboot_confirm %d`",
        token);
    if (written < 0 || (size_t)written >= size)
        return -1;

    return 0;
}

static int cmd_reboot_confirm(int argc, char *argv[],
                              long chat_id,
                              char *resp, size_t size,
                              response_type_t *resp_type) {
    
    if (resp_type) *resp_type = RESP_MARKDOWN;
  
    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    }
    
    if (argc < 2) {
        snprintf(resp, size, "Usage: /reboot_confirm <token>");
        return -1;
    }

    REQUIRE_ACCESS(chat_id, argv[0], resp, size);
  
    int token;

    if (parse_int(argv[1], &token) != 0) {
        snprintf(resp, size, "Invalid token format");
        return -1;
    }

    if (security_validate_reboot_token(chat_id, token) != 0) {
        snprintf(resp, size, "❌ Invalid or expired token");
        return -1;
    }

    g_reboot_requested_by = chat_id;   // 👈 КТО запросил ребут
    g_shutdown_requested = 2;

    snprintf(resp, size, "♻️ Rebooting...");
    return 0;
}

// ===== COMMANDS TABLE =====

static command_t commands[] = {
    {"/start", cmd_start, NULL, "General"},
    {"/help", cmd_help, NULL, "General"},

    {"/status", cmd_status, "System status", "System info"},
    {"/status_mini", cmd_status_mini, "Short status", "System info"},
    {"/about", cmd_about, "About bot", "System info"},
    {"/ping", cmd_ping, NULL, "System info"},

    {"/services", cmd_services, NULL, "Services"},
    {"/users", cmd_users, NULL, "Services"},
    {"/logs", cmd_logs, NULL, "Services"},

    {"/fail2ban", cmd_fail2ban, NULL, "Security"},

    {"/reboot", cmd_reboot, NULL, "System"},
    {"/reboot_confirm", cmd_reboot_confirm, NULL, NULL},
};

static const int commands_count =
    sizeof(commands) / sizeof(commands[0]);

// ===== HELP =====

static int cmd_help(int argc, char *argv[],
                    long chat_id,
                    char *resp, size_t size,
                    response_type_t *resp_type) {

    (void)argc; (void)argv; (void)chat_id;

    if (resp_type) *resp_type = RESP_MARKDOWN;

    int written = snprintf(resp, size, "*📚 COMMANDS*\n\n");

    if (written < 0 || (size_t)written >= size)
        return -1;

    size_t used = written;

    const char *current_category = NULL;

    for (int i = 0; i < commands_count; i++) {

        // 🛡️ защита от underflow
        if (used >= size - 1)
            break;

        if (!commands[i].category)
            continue;

        // 🔥 скрываем алиас /status_mini
        if (strcmp(commands[i].name, "/status_mini") == 0)
            continue;

        // 🔥 новая категория
        if (!current_category ||
            strcmp(current_category, commands[i].category) != 0) {

            int written = snprintf(resp + used, size - used,
                "%s*%s*\n",
                current_category ? "\n" : "",
                commands[i].category);

            if (written < 0 || (size_t)written >= size - used)
                break;

            used += written;
            current_category = commands[i].category;
          
        }

        const char *name = commands[i].name;

        // 👉 специальная отрисовка для /status
        if (strcmp(name, "/status") == 0) {

            int written = snprintf(resp + used, size - used,
                "/status\n"
                "  └ mini (/status_mini)\n");

            if (written < 0 || (size_t)written >= size - used)
                break;

            used += written;
            continue;
        }

        // 👉 обычные команды
        int written = snprintf(resp + used, size - used,
            "%s\n",
            name);

        if (written < 0 || (size_t)written >= size - used)
            break;

        used += written;
    }

    LOG_STATE(LOG_DEBUG, "help: building command list");
    LOG_STATE(LOG_INFO, "help generated (len=%zu)", used);
    
    return 0;
}

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

            if (!commands[i].handler) {
                snprintf(response, resp_size,
                         "Command not implemented");
                return -1;
            }

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
