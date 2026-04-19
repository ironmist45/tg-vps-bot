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

// 👇 Command handlers (exported)
// General → cmd_system.h
int cmd_start(int argc, char *argv[],
              long chat_id,
              char *response,
              size_t resp_size,
              response_type_t *resp_type);

// System info → cmd_system.h
int cmd_status(int argc, char *argv[],
               long chat_id,
               char *response,
               size_t resp_size,
               response_type_t *resp_type);

int cmd_status_mini(int argc, char *argv[],
                    long chat_id,
                    char *response,
                    size_t resp_size,
                    response_type_t *resp_type);

int cmd_about(int argc, char *argv[],
              long chat_id,
              char *response,
              size_t resp_size,
              response_type_t *resp_type);

int cmd_ping(int argc, char *argv[],
             long chat_id,
             char *response,
             size_t resp_size,
             response_type_t *resp_type);

// Services → cmd_services.h (создать, отсутствует)
int cmd_services(int argc, char *argv[],
                 long chat_id,
                 char *response,
                 size_t resp_size,
                 response_type_t *resp_type);

int cmd_users(int argc, char *argv[],
              long chat_id,
              char *response,
              size_t resp_size,
              response_type_t *resp_type);

int cmd_logs(int argc, char *argv[],
             long chat_id,
             char *response,
             size_t resp_size,
             response_type_t *resp_type);

// Control
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
