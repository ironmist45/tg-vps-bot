#ifndef COMMANDS_H
#define COMMANDS_H

#include <stddef.h>

typedef enum {
    RESP_MARKDOWN = 0,
    RESP_PLAIN = 1
} response_type_t;

int commands_handle(const char *text,
                    long chat_id,
                    char *response,
                    size_t resp_size,
                    response_type_t *resp_type);

#endif
