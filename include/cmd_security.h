#ifndef CMD_SECURITY_H
#define CMD_SECURITY_H

#include "commands.h"

// ==== 👇 Command handlers - Fail2Ban ====

int cmd_fail2ban(int argc, char *argv[],
                 long chat_id,
                 char *response,
                 size_t resp_size,
                 response_type_t *resp_type);

#endif
