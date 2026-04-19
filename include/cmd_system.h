#ifndef CMD_SYSTEM_H
#define CMD_SYSTEM_H

#include <stddef.h>
#include "commands.h"


// ==== 👇 Command handlers - /about and /ping commands ====

int cmd_about(int argc, char **argv,
              long chat_id,
              char *resp, size_t size,
              response_type_t *resp_type);

int cmd_ping(int argc, char **argv,
             long chat_id,
             char *resp, size_t size,
             response_type_t *resp_type);

#endif
