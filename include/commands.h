#ifndef COMMANDS_H
#define COMMANDS_H

#include <stddef.h>

typedef enum {
    RESP_MARKDOWN = 0,
    RESP_PLAIN = 1
} response_type_t;

// 👇 ДОБАВИЛИ
typedef int (*command_handler_t)(int argc, char *argv[],
                                long chat_id,
                                char *response,
                                size_t resp_size,
                                response_type_t *resp_type);

typedef struct {
    const char *name;
    command_handler_t handler;
    const char *description;
    const char *category;
} command_t;

// 👇 API
int commands_handle(const char *text,
                    long chat_id,
                    char *response,
                    size_t resp_size,
                    response_type_t *resp_type);

#endif
