#ifndef CMD_CONTROL_H
#define CMD_CONTROL_H

#include <stddef.h>
#include "commands.h"

// ===== 👇 Command handlers - Control commands =====

int cmd_reboot(int argc, char *argv[],
               long chat_id,
               char *response,
               size_t resp_size,
               response_type_t *resp_type);

int cmd_reboot_confirm(int argc, char *argv[],
                       long chat_id,
                       char *response,
                       size_t resp_size,
                       response_type_t *resp_type);

#endif
