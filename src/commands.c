#include "commands.h"
#include "logger.h"
#include "system.h"
#include "services.h"
#include "users.h"
#include "logs.h"
#include "security.h"
#include "version.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>

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

// ===== utils =====

static int split_args(char *input, char *argv[], int max_args) {
    int argc = 0;
    char *token = strtok(input, " ");
    while (token && argc < max_args) {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }
    return argc;
}

static int is_safe_ip(const char *ip) {
    for (size_t i = 0; ip[i]; i++) {
        if (!(isdigit((unsigned char)ip[i]) || ip[i] == '.'))
            return 0;
    }
    return 1;
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

    snprintf(resp, size,
        "*🚀 %s v%s (%s)*\n\n%s\n\n👉 /help",
        APP_NAME, APP_VERSION, APP_CODENAME, status);

    return 0;
}

static int cmd_status(int argc, char *argv[],
                     long chat_id,
                     char *resp, size_t size) {

    (void)argc; (void)argv;

    if (!security_is_allowed_chat(chat_id)) {
        snprintf(resp, size, "❌ Access denied");
        return -1;
    }

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

    snprintf(resp, size,
        "*ℹ️ ABOUT*\n\n"
        "%s v%s (%s)\n"
        "PID: %d\n"
        "Uptime: %dd %dh %dm",
        APP_NAME, APP_VERSION, APP_CODENAME,
        getpid(), days, hours, mins);

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

    snprintf(resp, size,
        "*🏓 PING*\n\n"
        "Response: `pong`\n"
        "Latency: `%ld ms`",
        ms);

    return 0;
}

// ===== SERVICES =====

static int cmd_services(int argc, char *argv[],
                       long chat_id,
                       char *resp, size_t size) {

    (void)argc; (void)argv;

    if (!security_is_allowed_chat(chat_id)) {
        snprintf(resp, size, "❌ Access denied");
        return -1;
    }

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

    if (!security_is_allowed_chat(chat_id)) {
        snprintf(resp, size, "❌ Access denied");
        return -1;
    }

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
        if (written < 0 || (size_t)written >= sizeof(args) - used)
            break;

        used += written;
    }

    if (users_get(resp, size) != 0) {
        snprintf(resp, size, "⚠️ Failed to get users");
        return -1;
}
return 0;
}

// ===== USERS =====

static int cmd_users(int argc, char *argv[],
                    long chat_id,
                    char *resp, size_t size) {

    (void)argc; (void)argv;

    if (!security_is_allowed_chat(chat_id)) {
        snprintf(resp, size, "❌ Access denied");
        return -1;
    }

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

    if (!security_is_allowed_chat(chat_id)) {
        snprintf(resp, size, "❌ Access denied");
        return -1;
    }

    if (argc < 2) {
        snprintf(resp, size,
            "*🛡 Fail2Ban*\n\n"
            "`/fail2ban status`\n"
            "`/fail2ban status sshd`\n"
            "`/fail2ban jail list`\n"
            "`/fail2ban ban <ip>`\n"
            "`/fail2ban unban <ip>`");
        return 0;
    }

    char cmd[256] = {0};

    if (strcmp(argv[1], "status") == 0) {

        if (argc == 2) {
            snprintf(cmd, sizeof(cmd),
                     "sudo -n /usr/local/bin/f2b-wrapper status 2>&1");
        } else if (argc >= 3 && strcmp(argv[2], "sshd") == 0) {
            snprintf(cmd, sizeof(cmd),
                     "sudo -n /usr/local/bin/f2b-wrapper status sshd 2>&1");
        } else {
            snprintf(resp, size, "Invalid status usage");
            return -1;
        }
    }

    else if (strcmp(argv[1], "jail") == 0 &&
             argc >= 3 &&
             strcmp(argv[2], "list") == 0) {

        snprintf(cmd, sizeof(cmd),
                 "sudo -n /usr/local/bin/f2b-wrapper status 2>&1");
    }

    else if (strcmp(argv[1], "ban") == 0 && argc >= 3) {

        if (!is_safe_ip(argv[2])) {
            snprintf(resp, size, "Invalid IP");
            return -1;
        }

        log_msg(LOG_WARN,
                "FAIL2BAN BAN: %s (chat_id=%ld)",
                argv[2], chat_id);

        snprintf(cmd, sizeof(cmd),
                 "sudo -n /usr/local/bin/f2b-wrapper set sshd banip %s 2>&1",
                 argv[2]);
    }

    else if (strcmp(argv[1], "unban") == 0 && argc >= 3) {

        if (!is_safe_ip(argv[2])) {
            snprintf(resp, size, "Invalid IP");
            return -1;
        }

        log_msg(LOG_WARN,
                "FAIL2BAN UNBAN: %s (chat_id=%ld)",
                argv[2], chat_id);

        snprintf(cmd, sizeof(cmd),
                 "sudo -n /usr/local/bin/f2b-wrapper set sshd unbanip %s 2>&1",
                 argv[2]);
    }

    else {
        snprintf(resp, size, "Invalid fail2ban command");
        return -1;
    }

    log_msg(LOG_INFO, "fail2ban exec: %s", cmd);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        snprintf(resp, size, "Execution failed");
        return -1;
    }

    resp[0] = '\0';
    size_t used = 0;

    while (fgets(resp + used, size - used, fp)) {
        used = strlen(resp);
        if (used >= size - 1)
            break;
    }

    pclose(fp);

    if (used == 0) {
        snprintf(resp, size, "No output (check sudo / wrapper)");
    }

    return 0;
}

// ===== REBOOT =====

static int cmd_reboot(int argc, char *argv[],
                     long chat_id,
                     char *resp, size_t size) {

    (void)argc; (void)argv;

    if (!security_is_allowed_chat(chat_id)) {
        snprintf(resp, size, "❌ Access denied");
        return -1;
    }

    int token = security_generate_reboot_token(chat_id);

    snprintf(resp, size,
        "⚠️ Confirm reboot:\n`/reboot_confirm %d`",
        token);

    return 0;
}

static int cmd_reboot_confirm(int argc, char *argv[],
                             long chat_id,
                             char *resp, size_t size) {
    
    (void)argv; // 🔥 убираем warning

    if (argc < 2) return -1;

    if (!security_is_allowed_chat(chat_id)) {
        snprintf(resp, size, "❌ Access denied");
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
    strncpy(buffer, text, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *argv[MAX_ARGS];
    int argc = split_args(buffer, argv, MAX_ARGS);

    if (argc == 0) {
        snprintf(response, resp_size, "Empty command");
        return -1;
    }

    for (int i = 0; i < commands_count; i++) {

        if (strcmp(argv[0], commands[i].name) == 0) {

            log_msg(LOG_INFO,
                    "Command: %s (chat_id=%ld)",
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

            // ===== LOG RESPONSE (🔥 НОВОЕ) =====
            if (response && response[0] != '\0') {

                char preview[256];

                strncpy(preview, response, sizeof(preview) - 1);
                preview[sizeof(preview) - 1] = '\0';

                // опционально: не логировать шумные команды
                if (strcmp(argv[0], "/logs") != 0) {
                    log_msg(LOG_DEBUG,
                            "Response to %s (chat_id=%ld): %s",
                            argv[0], chat_id, preview);
                }

            } else {
                log_msg(LOG_DEBUG,
                        "Response to %s (chat_id=%ld): <empty>",
                        argv[0], chat_id);
            }

            return rc;
        }
    }

    snprintf(response, resp_size, "Unknown command");
    return -1;
}
