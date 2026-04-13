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
#include <stddef.h>
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

// ===== commands =====

// ---------- START ----------
static int cmd_start(int argc, char *argv[],
                     long chat_id,
                     char *resp, size_t size) {
    (void)argc; (void)argv; (void)chat_id;

    char status[1024];

    if (system_get_status(status, sizeof(status)) != 0) {
        snprintf(status, sizeof(status), "⚠️ Failed to get system info");
    }

    snprintf(resp, size,
        "*🚀 %s v%s (%s)*\n\n%s\n\n👉 Use /help",
        APP_NAME, APP_VERSION, APP_CODENAME, status);

    return 0;
}

// ---------- LOGS ----------
static int cmd_logs(int argc, char *argv[],
                   long chat_id,
                   char *resp, size_t size) {
    (void)chat_id;

    char tmp[RESP_MAX];

    // 👉 /logs
    if (argc < 2) {
        if (logs_get(NULL, tmp, sizeof(tmp)) != 0) {
            snprintf(resp, size, "Failed to get services list");
            return -1;
        }
        snprintf(resp, size, "%s", tmp);
        return 0;
    }

    // 👉 склеиваем аргументы обратно: "ssh 100 error"
    char combined[256] = {0};

    for (int i = 1; i < argc; i++) {
        strcat(combined, argv[i]);
        if (i < argc - 1)
            strcat(combined, " ");
    }

    if (logs_get(combined, tmp, sizeof(tmp)) != 0) {
        snprintf(resp, size, "Failed to get logs");
        return -1;
    }

    snprintf(resp, size, "%s", tmp);
    return 0;
}

// ---------- HELP ----------
static int cmd_help(int argc, char *argv[],
                   long chat_id,
                   char *resp, size_t size);

// ---------- PING ----------
static int cmd_ping(int argc, char *argv[],
                   long chat_id,
                   char *resp, size_t size) {
    (void)argc; (void)argv; (void)chat_id;

    snprintf(resp, size, "pong");
    return 0;
}

// ---------- HELP IMPL ----------
static int cmd_help(int argc, char *argv[],
                   long chat_id,
                   char *resp, size_t size) {
    (void)argc; (void)argv; (void)chat_id;

    snprintf(resp, size,
        "*Commands:*\n"
        "/start\n"
        "/help\n"
        "/logs\n"
        "/logs ssh\n"
        "/logs ssh 100 error\n");

    return 0;
}

// ===== table =====

static command_t commands[] = {
    {"/start", cmd_start, "Start bot"},
    {"/help", cmd_help, "Help"},
    {"/ping", cmd_ping, "Ping"},
    {"/logs", cmd_logs, "Logs"},
};

static const int commands_count =
    sizeof(commands) / sizeof(commands[0]);

// ===== main handler =====

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
