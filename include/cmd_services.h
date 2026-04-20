#ifndef CMD_SERVICES_H
#define CMD_SERVICES_H

#include <stddef.h>
#include "commands.h"

// ===== 👇 Commands handlers V2 =====

int cmd_services_v2(command_ctx_t *ctx);
int cmd_users_v2(command_ctx_t *ctx);
int cmd_logs_v2(command_ctx_t *ctx);

// ===== 👇 Commands handlers Legacy =====

int cmd_services(int argc, char *argv[],
                 long chat_id,
                 char *response,
                 size_t resp_size,
                 response_type_t *resp_type);

int cmd_users(int argc, char *argv[],
              long chat_id,
              char *response,
              size_t resp_size,
              response_type_t *resp_type);

int cmd_logs(int argc, char *argv[],
             long chat_id,
             char *response,
             size_t resp_size,
             response_type_t *resp_type);

#endif
