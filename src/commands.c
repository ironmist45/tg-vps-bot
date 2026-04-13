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

#define MAX_ARGS 8
#define RESP_MAX 8192

extern time_t g_start_time;

typedef int (*command_handler_t)(int argc, char *argv[],
                                long chat_id,
                                char *response, size_t resp_size);

typedef struct {
    const char *name;
    command_handler_t handler;
    const char *description;
} command_t;

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

static void safe_write(char *dst, size_t size, const char *text) {
    snprintf(dst, size, "%s", text);
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

// ===== HELP =====

static int cmd_help(int argc, char *argv[],
                   long chat_id,
                   char *resp, size_t size);

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
        "🆔 PID: %d\n"
        "⏱ Uptime: %dd %dh %dm",
        APP_NAME,
        APP_VERSION,
        APP_CODENAME,
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

    snprintf(resp, size,
        "*🏓 PING*\n\npong");

    return 0;
}

// ===== ECHO =====

static int cmd_echo(int argc, char *argv[],
                   long chat_id,
                   char *resp, size_t size) {

    (void)chat_id;

    if (argc < 2) {
        safe_write(resp, size, "*Usage:* `/echo <text>`");
        return -1;
    }

    size_t used = 0;
    resp[0] = '\0';

    for (int i = 1; i < argc; i++) {
        int written = snprintf(resp + used, size - used,
                               "%s%s",
                               argv[i],
                               (i < argc - 1) ? " " : "");

        if (written < 0 || (size_t)written >= size - used)
            break;

        used += written;
    }

    return 0;
}

// ===== STATUS =====

static int cmd_status(int argc, char *argv[],
                     long chat_id,
                     char *resp, size_t size) {

    (void)argc; (void)argv; (void)chat_id;

    char tmp[1024];

    if (system_get_status(tmp, sizeof(tmp)) != 0) {
        snprintf(resp, size, "❌ Failed to get system status");
        return -1;
    }

    snprintf(resp, size,
        "*📊 STATUS*\n\n%s",
        tmp
    );

    return 0;
}

// ===== SERVICES =====

static int cmd_services(int argc, char *argv[],
                       long chat_id,
                       char *resp, size_t size) {

    (void)argc; (void)argv; (void)chat_id;

    char tmp[1024];

    if (services_get_status(tmp, sizeof(tmp)) != 0) {
        snprintf(resp, size, "❌ Failed to get services status");
        return -1;
    }

    snprintf(resp, size,
        "*🧩 SERVICES*\n\n%s",
        tmp
    );

    return 0;
}

// ===== USERS =====

static int cmd_users(int argc, char *argv[],
                    long chat_id,
                    char *resp, size_t size) {

    (void)argc; (void)argv; (void)chat_id;

    char tmp[1024];

    if (users_get_logged(tmp, sizeof(tmp)) != 0) {
        snprintf(resp, size, "❌ Failed to get users");
        return -1;
    }

    snprintf(resp, size,
        "*👥 USERS*\n\n%s",
        tmp
    );

    return 0;
}

// ===== LOGS =====

static int cmd_logs(int argc, char *argv[],
                   long chat_id,
                   char *resp, size_t size) {

    (void)chat_id;

    if (argc < 2) {
        snprintf(resp, size,
            "*📜 LOGS MENU*\n\n"

            "*📌 Available services:*\n"
            "`/logs ssh` — SSH daemon logs\n"
            "`/logs mtg` — MTProto proxy logs\n"
            "`/logs shadowsocks` — Shadowsocks logs\n\n"

            "*📊 Output control:*\n"
            "`/logs <service> <N>` — last N lines\n"
            "Example: `/logs ssh 100`\n\n"

            "*🔍 Filtering:*\n"
            "`/logs <service> error` — only errors\n"
            "`/logs <service> 100 error` — last 100 lines with filter\n\n"

            "*📎 Examples:*\n"
            "`/logs ssh`\n"
            "`/logs ssh 50`\n"
            "`/logs ssh error`\n"
            "`/logs ssh 100 error`"
        );
        return 0;
    }

    char args[256] = {0};

    for (int i = 1; i < argc; i++) {
        strcat(args, argv[i]);
        if (i < argc - 1) strcat(args, " ");
    }

    char tmp[RESP_MAX];

    if (logs_get(args, tmp, sizeof(tmp)) != 0) {
        snprintf(resp, size, "❌ Failed to get logs");
        return -1;
    }

    snprintf(resp, size, "%s", tmp);
    return 0;
}

// ===== REBOOT =====

static int cmd_reboot(int argc, char *argv[],
                     long chat_id,
                     char *resp, size_t size) {

    int token = security_generate_reboot_token(chat_id);

    if (token < 0) {
        snprintf(resp, size, "⏱ Too many requests, try later");
        return -1;
    }

    snprintf(resp, size,
        "⚠️ *REBOOT*\n\n"
        "Confirm:\n"
        "`/reboot_confirm %d`",
        token);

    return 0;
}

static int cmd_reboot_confirm(int argc, char *argv[],
                             long chat_id,
                             char *resp, size_t size) {

    if (argc < 2) {
        snprintf(resp, size, "*Usage:* `/reboot_confirm <token>`");
        return -1;
    }

    int token = atoi(argv[1]);

    if (security_validate_reboot_token(chat_id, token) != 0) {
        snprintf(resp, size, "❌ Invalid token");
        return -1;
    }

    log_msg(LOG_WARN, "REBOOT by chat_id=%ld", chat_id);

    system("reboot");

    snprintf(resp, size, "♻️ Rebooting...");
    return 0;
}

// ===== таблица =====

static command_t commands[] = {
    {"/start", cmd_start, "Start bot"},
    {"/help", cmd_help, "Show help"},
    {"/about", cmd_about, "About bot"},
    {"/ping", cmd_ping, "Ping"},
    {"/echo", cmd_echo, "Echo text"},
    {"/status", cmd_status, "System status"},
    {"/services", cmd_services, "Services"},
    {"/users", cmd_users, "Users"},
    {"/logs", cmd_logs, "Logs"},
    {"/reboot", cmd_reboot, "Reboot server"},
    {"/reboot_confirm", cmd_reboot_confirm, "Confirm reboot"},
};

static const int commands_count =
    sizeof(commands) / sizeof(commands[0]);

// ===== HELP =====

static int cmd_help(int argc, char *argv[],
                   long chat_id,
                   char *resp, size_t size) {

    size_t used = 0;

    snprintf(resp, size,
        "*📚 COMMANDS*\n\n");

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

// ===== dispatcher =====

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
