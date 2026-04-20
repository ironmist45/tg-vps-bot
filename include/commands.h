#ifndef COMMANDS_H
#define COMMANDS_H

#include <stddef.h>
#include <time.h>

typedef enum {
    RESP_MARKDOWN = 0,
    RESP_PLAIN = 1
} response_type_t;

// ===== LEGACY HANDLER =====
typedef int (*command_handler_t)(int argc, char *argv[],
                                long chat_id,
                                char *response,
                                size_t resp_size,
                                response_type_t *resp_type);

// ===== V2 CONTEXT =====
typedef struct {
    long chat_id;
    int user_id;
    const char *username;
    const char *args;
    const char *raw_text;

    time_t msg_date;   // 👈 cmd_ping_v2

    // 🔥 bridge to legacy response system
    char *response;
    size_t resp_size;
    response_type_t *resp_type;
} command_ctx_t;

// ===== V2 HANDLER =====
typedef int (*command_handler_v2_t)(command_ctx_t *ctx);

// ===== COMMAND STRUCT =====
typedef struct {
    const char *name;
    command_handler_t handler;
    command_handler_v2_t handler_v2;
    const char *description;
    const char *category;
} command_t;

// ===== API =====
int commands_handle(const char *text,
                    long chat_id,
                    char *response,
                    size_t resp_size,
                    response_type_t *resp_type);

#endif
