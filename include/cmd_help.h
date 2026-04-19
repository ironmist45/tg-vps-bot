#ifndef CMD_HELP_H
#define CMD_HELP_H

#include "commands.h"

// ==== 👇 Command handlers - /help command ====

int cmd_help(int argc, char *argv[],
             long chat_id,
             char *response,
             size_t resp_size,
             response_type_t *resp_type);

#endif
