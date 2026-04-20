#ifndef CMD_SYSTEM_H
#define CMD_SYSTEM_H

#include <stddef.h>
#include "commands.h"


// ==== 👇 Command handlers v2 - General & System info ====
// Bot commands: /start, /status, /health,
// ============  /about and /ping

int cmd_about(int argc, char **argv,
              long chat_id,
              char *resp, size_t size,
              response_type_t *resp_type);

int cmd_start_v2(command_ctx_t *ctx);
int cmd_status_v2(command_ctx_t *ctx);
int cmd_health_v2(command_ctx_t *ctx);
int cmd_ping_v2(command_ctx_t *ctx);

#endif
