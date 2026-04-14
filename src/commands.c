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

static int cmd_help(int argc, char *argv[],
                   long chat_id,
                   char *resp, size_t size);

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

// 🔥 простая проверка IP (IPv4)
static int is_valid_ip(const char *ip) {
    if (!ip) return 0;

    int dots = 0;
    for (const char *p = ip; *p; p++) {
        if (*p == '.') dots++;
        else if (!isdigit(*p)) return 0;
    }

    if (dots != 3) return 0;
    return 1;
}

// ===== START =====

static int cmd_start(int argc, char *argv[],
                    long chat_id,
                    char *resp, size_t size) {

    (void)argc; (void)argv; (void)chat_id;

    char status[1024];

    if (system_get_status(status, sizeof(status)) != 0) {
        snprintf(status, sizeof(status), "⚠️ Failed to get system info");
    }

    snprintf(resp, size,
        "*🚀 %s v%s (%s)*\n\n"
        "%s\n\n"
        "👉 Use /help to see commands",
        APP_NAME,
        APP_VERSION,
        APP_CODENAME,
        status
    );

    return 0;
}

// ===== STATUS =====

static int cmd_status(int argc, char *argv[],
                     long chat_id,
                     char *resp, size_t size) {

    (void)argc; (void)argv;

    if (!security_is_allowed_chat(chat_id)) {
        snprintf(resp, size, "❌ Access denied");
        return -1;
    }

    return system_get_status(resp, size);
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

    return users_get(resp, size);
}

// ===== FAIL2BAN =====

static int cmd_fail2ban(int argc, char *argv[],
                       long chat_id,
                       char *resp, size_t size) {

    if (!security_is_allowed_chat(chat_id)) {
        snprintf(resp, size, "❌ Access denied");
        return -1;
    }

    if (argc < 2) {
        snprintf(resp, size,
            "*🛡 FAIL2BAN*\n\n"
            "`/fail2ban status`\n"
            "`/fail2ban sshd`\n"
            "`/fail2ban jail list`\n"
            "`/ban <IP>`\n"
            "`/unban <IP>`\n"
        );
        return 0;
    }

    FILE *fp = NULL;

    if (strcmp(argv[1], "status") == 0) {
        fp = popen("fail2ban-client status 2>/dev/null", "r");

    } else if (strcmp(argv[1], "sshd") == 0) {
        fp = popen("fail2ban-client status sshd 2>/dev/null", "r");

    } else if (strcmp(argv[1], "jail") == 0 && argc >= 3 &&
               strcmp(argv[2], "list") == 0) {

        fp = popen("fail2ban-client status | grep 'Jail list'", "r");

    } else {
        snprintf(resp, size, "Unknown option");
        return -1;
    }

    if (!fp) {
        snprintf(resp, size, "❌ Failed to execute fail2ban");
        return -1;
    }

    char line[256];
    resp[0] = '\0';

    while (fgets(line, sizeof(line), fp)) {
        if (strlen(resp) + strlen(line) >= size - 1)
            break;
        strcat(resp, line);
    }

    pclose(fp);
    return 0;
}

// ===== BAN =====

static int cmd_ban(int argc, char *argv[],
                   long chat_id,
                   char *resp, size_t size) {

    if (!security_is_allowed_chat(chat_id)) {
        snprintf(resp, size, "❌ Access denied");
        return -1;
    }

    if (argc < 2 || !is_valid_ip(argv[1])) {
        snprintf(resp, size, "Usage: /ban <IP>");
        return -1;
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "fail2ban-client set sshd banip %s 2>/dev/null", argv[1]);

    int ret = system(cmd);

    if (ret != 0) {
        snprintf(resp, size, "❌ Ban failed");
        return -1;
    }

    snprintf(resp, size, "🚫 Banned: %s", argv[1]);
    return 0;
}

// ===== UNBAN =====

static int cmd_unban(int argc, char *argv[],
                     long chat_id,
                     char *resp, size_t size) {

    if (!security_is_allowed_chat(chat_id)) {
        snprintf(resp, size, "❌ Access denied");
        return -1;
    }

    if (argc < 2 || !is_valid_ip(argv[1])) {
        snprintf(resp, size, "Usage: /unban <IP>");
        return -1;
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "fail2ban-client set sshd unbanip %s 2>/dev/null", argv[1]);

    int ret = system(cmd);

    if (ret != 0) {
        snprintf(resp, size, "❌ Unban failed");
        return -1;
    }

    snprintf(resp, size, "✅ Unbanned: %s", argv[1]);
    return 0;
}

// ===== ABOUT =====

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
        "*%s v%s (%s)*\n\n"
        "👤 Author: %s\n"
        "📅 Year: %s\n\n"
        "⚙️ Build: %s %s\n"
        "🖥 Target: %s\n\n"
        "🆔 PID: %d\n"
        "⏱ Uptime: %dd %dh %dm",
        APP_NAME,
        APP_VERSION,
        APP_CODENAME,
        APP_AUTHOR,
        APP_YEAR,
        BUILD_DATE,
        BUILD_TIME,
        TARGET_OS,
        getpid(),
        days, hours, mins
    );

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
        ms
    );

    return 0;
}

// ===== COMMAND TABLE =====

static command_t commands[] = {
    {"/start", cmd_start, "Start bot"},
    {"/help", cmd_help, "Show help"},
    {"/status", cmd_status, "System status"},
    {"/users", cmd_users, "Active users"},
    {"/fail2ban", cmd_fail2ban, "Fail2Ban info"},
    {"/ban", cmd_ban, "Ban IP"},
    {"/unban", cmd_unban, "Unban IP"},
    {"/about", cmd_about, "About bot"},
    {"/ping", cmd_ping, "Ping"},
    {"/services", cmd_services, "List services"},
    {"/logs", cmd_logs, "Logs"},
    {"/reboot", cmd_reboot, "Reboot"},
    {"/reboot_confirm", cmd_reboot_confirm, "Confirm reboot"},
};

static const int commands_count =
    sizeof(commands) / sizeof(commands[0]);

// ===== HELP =====

static int cmd_help(int argc, char *argv[],
                   long chat_id,
                   char *resp, size_t size) {

    (void)argc; (void)argv; (void)chat_id;

    size_t used = 0;

    snprintf(resp, size, "*📚 COMMANDS*\n\n");
    used = strlen(resp);

    for (int i = 0; i < commands_count; i++) {

        int written = snprintf(resp + used, size - used,
                               "`%s` — %s\n",
                               commands[i].name,
                               commands[i].description);

        if (written < 0 || (size_t)written >= size - used)
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

            return commands[i].handler(
                argc, argv, chat_id, response, resp_size
            );
        }
    }

    snprintf(response, resp_size, "Unknown command");
    return -1;
}
