#include "commands.h"
#include "logger.h"
#include "system.h"
#include "services.h"
#include "users.h"
#include "logs.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define MAX_ARGS 8
#define TOKEN_TTL 60
#define REBOOT_COOLDOWN 300  // 5 минут

typedef int (*command_handler_t)(long chat_id,
                                int argc, char *argv[],
                                char *response, size_t resp_size);

typedef struct {
    const char *name;
    command_handler_t handler;
    const char *description;
} command_t;

// ===== REBOOT STATE =====

static char reboot_token[16] = {0};
static time_t reboot_token_time = 0;
static long reboot_chat_id = 0;

static time_t last_reboot_time = 0;

static void clear_reboot_token() {
    reboot_token[0] = '\0';
    reboot_token_time = 0;
    reboot_chat_id = 0;
}

static void generate_token(char *out, size_t size) {
    snprintf(out, size, "%06d", rand() % 1000000);
}

// ===== UTILS =====

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

// ===== COMMANDS =====

static int cmd_start(long chat_id, int argc, char *argv[], char *resp, size_t size) {
    (void)chat_id; (void)argc; (void)argv;

    safe_write(resp, size,
        "tg_bot is running\nUse /help");

    return 0;
}

static int cmd_help(long chat_id, int argc, char *argv[], char *resp, size_t size);

static int cmd_ping(long chat_id, int argc, char *argv[], char *resp, size_t size) {
    (void)chat_id; (void)argc; (void)argv;
    safe_write(resp, size, "pong");
    return 0;
}

static int cmd_echo(long chat_id, int argc, char *argv[], char *resp, size_t size) {
    (void)chat_id;

    if (argc < 2) {
        safe_write(resp, size, "Usage: /echo <text>");
        return -1;
    }

    resp[0] = '\0';

    for (int i = 1; i < argc; i++) {
        strncat(resp, argv[i], size - strlen(resp) - 2);
        if (i < argc - 1)
            strncat(resp, " ", size - strlen(resp) - 2);
    }

    return 0;
}

// ===== STATUS =====

static int cmd_status(long chat_id, int argc, char *argv[], char *resp, size_t size) {
    (void)chat_id; (void)argc; (void)argv;

    if (system_get_status(resp, size) != 0) {
        snprintf(resp, size, "Failed to get system status");
        return -1;
    }

    return 0;
}

// ===== SERVICES =====

static int cmd_services(long chat_id, int argc, char *argv[], char *resp, size_t size) {
    (void)chat_id; (void)argc; (void)argv;

    if (services_get_status(resp, size) != 0) {
        snprintf(resp, size, "Failed");
        return -1;
    }

    return 0;
}

// ===== USERS =====

static int cmd_users(long chat_id, int argc, char *argv[], char *resp, size_t size) {
    (void)chat_id; (void)argc; (void)argv;

    if (users_get_logged(resp, size) != 0) {
        snprintf(resp, size, "Failed");
        return -1;
    }

    return 0;
}

// ===== LOGS =====

static int cmd_logs(long chat_id, int argc, char *argv[], char *resp, size_t size) {
    (void)chat_id;

    if (argc < 2) {
        snprintf(resp, size, "Usage: /logs <service>");
        return -1;
    }

    return logs_get(argv[1], resp, size);
}

// ===== REBOOT =====

static int cmd_reboot(long chat_id, int argc, char *argv[], char *resp, size_t size) {
    (void)argc; (void)argv;

    time_t now = time(NULL);

    if ((now - last_reboot_time) < REBOOT_COOLDOWN) {
        snprintf(resp, size, "⏱ Cooldown active. Try later.");
        return -1;
    }

    clear_reboot_token();

    generate_token(reboot_token, sizeof(reboot_token));
    reboot_token_time = now;
    reboot_chat_id = chat_id;

    log_msg(LOG_WARN,
        "Reboot requested by chat_id=%ld token=%s",
        chat_id, reboot_token);

    snprintf(resp, size,
        "⚠ Reboot requested\n\n"
        "Confirm:\n/reboot_confirm %s\n\n"
        "Valid: %d sec",
        reboot_token, TOKEN_TTL);

    return 0;
}

static int cmd_reboot_confirm(long chat_id, int argc, char *argv[], char *resp, size_t size) {

    if (argc < 2) {
        snprintf(resp, size, "Usage: /reboot_confirm <token>");
        return -1;
    }

    time_t now = time(NULL);

    if (reboot_token[0] == '\0') {
        snprintf(resp, size, "❌ No active token");
        return -1;
    }

    if (chat_id != reboot_chat_id) {
        log_msg(LOG_WARN,
            "Unauthorized confirm attempt chat_id=%ld",
            chat_id);

        snprintf(resp, size, "❌ Not allowed");
        return -1;
    }

    if (strcmp(argv[1], reboot_token) != 0) {
        log_msg(LOG_WARN, "Invalid token from chat_id=%ld", chat_id);
        snprintf(resp, size, "❌ Invalid token");
        return -1;
    }

    if ((now - reboot_token_time) > TOKEN_TTL) {
        clear_reboot_token();
        snprintf(resp, size, "❌ Token expired");
        return -1;
    }

    log_msg(LOG_WARN,
        "Reboot confirmed by chat_id=%ld",
        chat_id);

    clear_reboot_token();
    last_reboot_time = now;

    if (system("reboot") == -1) {
        log_msg(LOG_ERROR, "Reboot failed");
        snprintf(resp, size, "Reboot failed");
        return -1;
    }

    snprintf(resp, size, "🔄 Rebooting...");
    return 0;
}

// ===== TABLE =====

static command_t commands[] = {
    {"/start", cmd_start, "Start"},
    {"/help", cmd_help, "Help"},
    {"/ping", cmd_ping, "Ping"},
    {"/echo", cmd_echo, "Echo"},
    {"/status", cmd_status, "System"},
    {"/services", cmd_services, "Services"},
    {"/users", cmd_users, "Users"},
    {"/logs", cmd_logs, "Logs"},
    {"/reboot", cmd_reboot, "Reboot"},
    {"/reboot_confirm", cmd_reboot_confirm, "Confirm reboot"},
};

static const int commands_count =
    sizeof(commands) / sizeof(commands[0]);

// ===== HELP =====

static int cmd_help(long chat_id, int argc, char *argv[], char *resp, size_t size) {
    (void)chat_id; (void)argc; (void)argv;

    resp[0] = '\0';

    for (int i = 0; i < commands_count; i++) {
        strncat(resp, commands[i].name, size - strlen(resp) - 1);
        strncat(resp, " - ", size - strlen(resp) - 1);
        strncat(resp, commands[i].description, size - strlen(resp) - 1);
        strncat(resp, "\n", size - strlen(resp) - 1);
    }

    return 0;
}

// ===== HANDLER =====

int commands_handle(long chat_id,
                    const char *text,
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
                "Command: %s from chat_id=%ld",
                argv[0], chat_id);

            return commands[i].handler(
                chat_id, argc, argv, response, resp_size);
        }
    }

    snprintf(response, resp_size, "Unknown command");
    return -1;
}
