#include "commands.h"
#include "services.h"
#include "users.h"
#include "logs.h"
#include "utils.h"

#include <stdio.h>

// ==== COMMANDS: Services ====
// Bot commands:  /services,
// ============   /users, /logs

// ==== /services command ====

int cmd_services(int argc, char *argv[],
                 long chat_id,
                 char *resp, size_t size,
                 response_type_t *resp_type) {
  
  (void)argc;    // unused
  (void)chat_id; // unused

    if (resp_type) *resp_type = RESP_MARKDOWN;

    if (validate_command(argv, resp, size) != 0)
        return -1;
  
    if (services_get_status(resp, size) != 0) {
        snprintf(resp, size, "⚠️ Failed to get services");
        return -1;
    }
  
return 0;
  
}

// ==== /services v2 command ====

int cmd_services_v2(command_ctx_t *ctx)
{
    if (ctx->resp_type) {
        *(ctx->resp_type) = RESP_MARKDOWN;
    }

    if (services_get_status(ctx->response, ctx->resp_size) != 0) {
        snprintf(ctx->response, ctx->resp_size,
                 "⚠️ Failed to get services");
        return -1;
    }

    return 0;
}

// ==== /users command ====

int cmd_users(int argc, char *argv[],
                  long chat_id,
                  char *resp, size_t size,
                  response_type_t *resp_type) {
  
  (void)argc;    // unused
  (void)chat_id; // unused
  
    if (resp_type) *resp_type = RESP_MARKDOWN;

    if (validate_command(argv, resp, size) != 0)
        return -1;
       
    if (users_get(resp, size) != 0) {
        snprintf(resp, size, "⚠️ Failed to get users");
        return -1;
    }
  
return 0;
  
}

// ==== /users v2 command ====

int cmd_users_v2(command_ctx_t *ctx)
{
    if (ctx->resp_type) {
        *(ctx->resp_type) = RESP_MARKDOWN;
    }

    if (users_get(ctx->response, ctx->resp_size) != 0) {
        snprintf(ctx->response, ctx->resp_size,
                 "⚠️ Failed to get users");
        return -1;
    }

    return 0;
}

// ==== /logs command ====

int cmd_logs(int argc, char *argv[],
                 long chat_id,
                 char *resp, size_t size,
                 response_type_t *resp_type) {
  
  (void)chat_id; // unused

    if (resp_type) *resp_type = RESP_PLAIN;

    if (validate_command(argv, resp, size) != 0)
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

    if (logs_get(args, resp, size) != 0) {
        snprintf(resp, size, "⚠️ Failed to get logs");
        return -1;
    }

    return 0;
}
