#ifndef CMD_SERVICES_H
#define CMD_SERVICES_H

#include <stddef.h>
#include "commands.h"

// ===== 👇 Commands handlers V2 =====

int cmd_services_v2(command_ctx_t *ctx);
int cmd_users_v2(command_ctx_t *ctx);
int cmd_logs_v2(command_ctx_t *ctx);

#endif
