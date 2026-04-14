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

// ===== SERVICES (🔥 НОВОЕ) =====

static int cmd_services(int argc, char *argv[],
                       long chat_id,
                       char *resp, size_t size) {

    (void)argc; (void)argv;

    if (!security_is_allowed_chat(chat_id)) {
        snprintf(resp, size, "❌ Access denied");
        return -1;
    }

    char tmp[RESP_MAX];

    if (services_get_status(tmp, sizeof(tmp)) != 0) {
        snprintf(resp, size, "❌ Failed to get services");
        return -1;
    }

    snprintf(resp, size, "%s", tmp);
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
            "`/logs ssh`\n"
            "`/logs mtg`\n"
            "`/logs shadowsocks`\n\n"
            "`/logs <service> <N>`\n"
            "`/logs <service> error`\n"
        );
        return 0;
    }

    char args[256] = {0};
    size_t used = 0;

    for (int i = 1; i < argc; i++) {
        int written = snprintf(args + used, sizeof(args) - used,
                               "%s%s",
                               argv[i],
                               (i < argc - 1) ? " " : "");

        if (written < 0 || (size_t)written >= sizeof(args) - used)
            break;

        used += written;
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

    (void)argc; (void)argv;

    if (!security_is_allowed_chat(chat_id)) {
        snprintf(resp, size, "❌ Access denied");
        return -1;
    }

    int token = security_generate_reboot_token(chat_id);

    if (token < 0) {
        snprintf(resp, size, "⏱ Too many requests");
        return -1;
    }

    snprintf(resp, size,
        "⚠️ REBOOT\nConfirm:\n/reboot_confirm %d",
        token);

    return 0;
}

// ===== REBOOT CONFIRM =====

static int cmd_reboot_confirm(int argc, char *argv[],
                             long chat_id,
                             char *resp, size_t size) {

    if (argc < 2) {
        snprintf(resp, size, "Usage: /reboot_confirm <token>");
        return -1;
    }

    if (!security_is_allowed_chat(chat_id)) {
        snprintf(resp, size, "❌ Access denied");
        return -1;
    }

    char *endptr = NULL;
    long token = strtol(argv[1], &endptr, 10);

    if (*endptr != '\0') {
        snprintf(resp, size, "Invalid token");
        return -1;
    }

    if (security_validate_reboot_token(chat_id, (int)token) != 0) {
        snprintf(resp, size, "❌ Invalid or expired token");
        return -1;
    }

    log_msg(LOG_WARN, "REBOOT CONFIRMED by chat_id=%ld", chat_id);

    g_shutdown_requested = 2;

    snprintf(resp, size, "♻️ Reboot scheduled...");
    return 0;
}

// ===== COMMAND TABLE =====

static command_t commands[] = {
    {"/start", cmd_start, "Start bot"},
    {"/help", NULL, "Show help"},
    {"/about", cmd_about, "About bot"},
    {"/ping", cmd_ping, "Ping"},
    {"/services", cmd_services, "List services"}, // 🔥 FIX
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

            if (commands[i].handler) {
                return commands[i].handler(
                    argc, argv, chat_id, response, resp_size
                );
            }
        }
    }

    snprintf(response, resp_size, "Unknown command");
    return -1;
}
