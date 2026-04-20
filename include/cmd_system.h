#ifndef CMD_SYSTEM_H
#define CMD_SYSTEM_H

#include <stddef.h>
#include "commands.h"


// ==== 👇 Command handlers - General & System info ====
// Bot commands: /start, /status, /status_mini,
// ============  /about and /ping

int cmd_start(int argc, char *argv[],
              long chat_id,
              char *response,
              size_t resp_size,
              response_type_t *resp_type);

int cmd_status(int argc, char *argv[],
               long chat_id,
               char *response,
               size_t resp_size,
               response_type_t *resp_type);

int cmd_status_mini(int argc, char *argv[],
                    long chat_id,
                    char *response,
                    size_t resp_size,
                    response_type_t *resp_type);

int cmd_about(int argc, char **argv,
              long chat_id,
              char *resp, size_t size,
              response_type_t *resp_type);

int cmd_ping(int argc, char **argv,
             long chat_id,
             char *resp, size_t size,
             response_type_t *resp_type);

int cmd_ping_v2(command_ctx_t *ctx);

#endif
