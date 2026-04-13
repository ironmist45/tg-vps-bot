#include <stddef.h>

#ifndef COMMANDS_H
#define COMMANDS_H

int commands_handle(long chat_id,
                    const char *text,
                    char *response,
                    size_t resp_size);

#endif
