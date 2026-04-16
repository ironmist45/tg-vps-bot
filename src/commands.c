#include "commands.h"
#include "logger.h"
#include "system.h"
#include "services.h"
#include "users.h"
#include "logs.h"
#include "security.h"
#include "version.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <arpa/inet.h>

#define MAX_ARGS 8
#define RESP_MAX 8192

extern time_t g_start_time;
extern volatile sig_atomic_t g_shutdown_requested;
extern long g_reboot_requested_by;

// ===== types =====

typedef int (*command_handler_t)(int argc, char *argv[],
                                long chat_id,
                                char *response, size_t resp_size);

typedef struct {
    const char *name;
    command_handler_t handler;
    const char *description;
} command_t;

// ===== forward =====

static int cmd_help(int, char **, long, char *, size_t);
static int cmd_start(int, char **, long, char *, size_t);
static int cmd_status(int, char **, long, char *, size_t);
static int cmd_about(int, char **, long, char *, size_t);
static int cmd_ping(int, char **, long, char *, size_t);
static int cmd_services(int, char **, long, char *, size_t);
static int cmd_logs(int, char **, long, char *, size_t);
static int cmd_users(int, char **, long, char *, size_t);
static int cmd_fail2ban(int, char **, long, char *, size_t);
static int cmd_reboot(int, char **, long, char *, size_t);
static int cmd_reboot_confirm(int, char **, long, char *, size_t);

// ==== exec ====

static int exec_command(char *const argv[],
                        char *resp,
                        size_t size) {

    int pipefd[2];

    if (pipe(pipefd) < 0) {
        return -1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        // ===== CHILD =====

        // stdout -> pipe
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
            dup2(pipefd[1], STDERR_FILENO) < 0) {
            _exit(127);
        }

        close(pipefd[0]);
        close(pipefd[1]);

        // ===== DEBUG: build full command line =====
        char cmdline[256] = {0};
        size_t used = 0;

        for (int i = 0; argv[i]; i++) {

            int written = snprintf(cmdline + used,
                                   sizeof(cmdline) - used,
                                   "%s%s",
                                   argv[i],
                                   argv[i + 1] ? " " : "");

            if (written < 0 || (size_t)written >= sizeof(cmdline) - used) {
                LOG_CMD(LOG_WARN, "exec cmdline truncated");
                break;
            }

            used += (size_t)written;
      }

LOG_CMD(LOG_DEBUG, "exec: %s", cmdline);

        // ===== EXEC =====
        execv("/usr/bin/sudo", argv);

        // если exec не сработал
        _exit(127);
    }

    // ===== PARENT =====

    close(pipefd[1]);

    resp[0] = '\0';
    size_t used = 0;
    int lines = 0;

    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        close(pipefd[0]);
        return -1;
    }

    while (fgets(resp + used, size - used, fp)) {
        size_t len = strlen(resp + used);
        used += len;
        lines++;

        if (used >= size - 1) {
            strncat(resp, "\n...truncated...", size - strlen(resp) - 1);
            break;
        }
    }

    fclose(fp);

    int status;
    if (waitpid(pid, &status, 0) < 0) {
        log_msg(LOG_ERROR, "waitpid failed");
        return -1;
    }

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        log_msg(LOG_INFO, "exit code=%d", code);
} else {
    log_msg(LOG_WARN, "process terminated abnormally");
}

    log_msg(LOG_INFO,
        "exec done: status=%d, lines=%d, bytes=%zu",
        status, lines, used);

    if (used == 0) {
        snprintf(resp, size,
            "⚠️ No output (exit=%d)",
            WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    }

    return 0;
}

// ===== utils =====

static int is_safe_ip(const char *ip) {
    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip, &(sa.sin_addr)) == 1;
}

static int check_access(long chat_id,
                        const char *cmd,
                        char *resp,
                        size_t size) {

    if (!security_is_allowed_chat(chat_id)) {
        LOG_STATE(LOG_WARN,
                "ACCESS DENIED: cmd=%s (chat_id=%ld)",
                cmd, chat_id);

        snprintf(resp, size, "❌ Access denied");
        return -1;
    }

    return 0;
}

// ===== COMMANDS =====

static int cmd_start(int argc, char *argv[],
                    long chat_id,
                    char *resp, size_t size) {

    (void)argc; (void)argv; (void)chat_id;

    char status[1024];

    if (system_get_status(status, sizeof(status)) != 0) {
        snprintf(status, sizeof(status), "⚠️ Failed to get system info");
    }

    int written = snprintf(resp, size,
        "*🚀 %s v%s (%s)*\n\n%s\n\n👉 /help",
        APP_NAME, APP_VERSION, APP_CODENAME, status);
    if (written < 0 || (size_t)written >= size)
        return -1;

    return 0;
}

static int cmd_status(int argc, char *argv[],
                     long chat_id,
                     char *resp, size_t size) {
    (void)argc;

    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    }

    if (check_access(chat_id, argv[0], resp, size) != 0)
        return -1;
  
    if (system_get_status(resp, size) != 0) {
        snprintf(resp, size, "⚠️ Failed to get system status");
        return -1;
    }
  
return 0;
  
}

static int cmd_about(int argc, char *argv[],
                    long chat_id,
                    char *resp, size_t size) {

    (void)argc; (void)argv; (void)chat_id;

    time_t now = time(NULL);
    long uptime = now - g_start_time;

    int days = uptime / 86400;
    int hours = (uptime % 86400) / 3600;
    int mins = (uptime % 3600) / 60;

    int written = snprintf(resp, size,
        "*ℹ️ ABOUT*\n\n"
        "%s v%s (%s)\n"
        "PID: %d\n"
        "Uptime: %dd %dh %dm",
        APP_NAME, APP_VERSION, APP_CODENAME,
        getpid(), days, hours, mins);
    if (written < 0 || (size_t)written >= size)
        return -1;

    return 0;
}

// ===== PING =====

static int cmd_ping(int argc, char *argv[],
                   long chat_id,
                   char *resp, size_t size) {

    (void)argc; (void)argv; (void)chat_id;

    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    usleep(1000);
    clock_gettime(CLOCK_MONOTONIC, &end);

    long ms =
        (end.tv_sec - start.tv_sec) * 1000 +
        (end.tv_nsec - start.tv_nsec) / 1000000;

    int written = snprintf(resp, size,
        "*🏓 PING*\n\n"
        "Response: `pong`\n"
        "Latency: `%ld ms`",
        ms);
    if (written < 0 || (size_t)written >= size)
        return -1;

    return 0;
}

// ===== SERVICES =====

static int cmd_services(int argc, char *argv[],
                       long chat_id,
                       char *resp, size_t size) {
    (void)argc;

    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    }

    if (check_access(chat_id, argv[0], resp, size) != 0)
        return -1;
  
    if (services_get_status(resp, size) != 0) {
        snprintf(resp, size, "⚠️ Failed to get services");
        return -1;
    }
  
return 0;
  
}

// ===== LOGS =====

static int cmd_logs(int argc, char *argv[],
                   long chat_id,
                   char *resp, size_t size) {

    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    } 

    if (check_access(chat_id, argv[0], resp, size) != 0)
        return -1;

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
                    char *resp, size_t size) {
    (void)argc;

    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    }

    if (check_access(chat_id, argv[0], resp, size) != 0)
        return -1;
  
    if (users_get(resp, size) != 0) {
        snprintf(resp, size, "⚠️ Failed to get users");
        return -1;
    }
  
return 0;
  
}

// ===== FAIL2BAN (🔥 FIXED + LOGGING) =====

static int cmd_fail2ban(int argc, char *argv[],
                       long chat_id,
                       char *resp, size_t size) {

    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    }

    if (check_access(chat_id, argv[0], resp, size) != 0)
        return -1;

    if (argc < 2) {
        snprintf(resp, size,
            "*🛡 Fail2Ban*\n\n"
            "```\n"
            "/fail2ban status\n"
            "/fail2ban status sshd\n"
            "/fail2ban ban <ip>\n"
            "/fail2ban unban <ip>");
            "\n```");
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

        return exec_command(args, resp, size);
    }
    else if (argc >= 3 && strcmp(argv[2], "sshd") == 0) {
        char *const args[] = {
            "sudo",
            "-n",
            "/usr/local/bin/f2b-wrapper",
            "status",
            argv[2],
            NULL
        };

        return exec_command(args, resp, size);
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

        return exec_command(args, resp, size);
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

        return exec_command(args, resp, size);
}

    else {
        snprintf(resp, size, "Invalid fail2ban command");
        return -1;
    }
  
}
// ===== REBOOT =====

static int cmd_reboot(int argc, char *argv[],
                     long chat_id,
                     char *resp, size_t size) {
    (void)argc;

    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    }

    if (check_access(chat_id, argv[0], resp, size) != 0)
        return -1;

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
                             char *resp, size_t size) {
  
    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    }
    
    if (argc < 2) {
        snprintf(resp, size, "Usage: /reboot_confirm <token>");
        return -1;
    }

    if (check_access(chat_id, argv[0], resp, size) != 0)
        return -1;
  
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

// ===== COMMAND TABLE =====

static command_t commands[] = {
    {"/start", cmd_start, "Start bot"},
    {"/help", cmd_help, "Help"},
    {"/status", cmd_status, "System status"},
    {"/about", cmd_about, "About"},
    {"/ping", cmd_ping, "Ping"},
    {"/services", cmd_services, "Services"},
    {"/users", cmd_users, "Users"},
    {"/logs", cmd_logs, "Logs"},
    {"/fail2ban", cmd_fail2ban, "Fail2Ban"},
    {"/reboot", cmd_reboot, "Reboot"},
    {"/reboot_confirm", cmd_reboot_confirm, NULL},
};

static const int commands_count =
    sizeof(commands) / sizeof(commands[0]);

// ===== HELP =====

static int cmd_help(int argc, char *argv[],
                   long chat_id,
                   char *resp, size_t size) {

    (void)argc; (void)argv; (void)chat_id;

    size_t used = snprintf(resp, size, "*📚 COMMANDS*\n\n");

    for (int i = 0; i < commands_count; i++) {

        if (!commands[i].description)
            continue;

        int written = snprintf(resp + used, size - used,
                               "`%s` — %s\n",
                               commands[i].name,
                               commands[i].description);

        if (written < 0 ||
            (size_t)written >= size - used)
            break;

        used += written;
    }

    return 0;
}
// ===== DISPATCHER =====

int commands_handle(const char *text,
                    long chat_id,
                    char *response,
                    size_t resp_size) {

    if (!text || text[0] != '/') {
        snprintf(response, resp_size, "Invalid command");
        return -1;
    }

    char buffer[512];
  
    if (safe_copy(buffer, sizeof(buffer), text) != 0) {
        snprintf(response, resp_size, "Command too long");
        return -1;
    }
  
    char *argv[MAX_ARGS];
    int argc = split_args(buffer, argv, MAX_ARGS);

    if (argc == 0) {
        snprintf(response, resp_size, "Empty command");
        return -1;
    }

    for (int i = 0; i < commands_count; i++) {

        if (strcmp(argv[0], commands[i].name) == 0) {

            log_msg(LOG_INFO,
                    "CMD %s (chat_id=%ld)",
                    argv[0], chat_id);

            if (!commands[i].handler) {
                snprintf(response, resp_size,
                         "Command not implemented");
                return -1;
            }

            int rc = commands[i].handler(
                argc, argv, chat_id, response, resp_size
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

            return rc;
        }
    }

    log_msg(LOG_WARN,
        "UNKNOWN CMD %s (chat_id=%ld)",
        argv[0], chat_id);

snprintf(response, resp_size, "Unknown command");
return -1;
}
