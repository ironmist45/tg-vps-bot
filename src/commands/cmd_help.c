#include "cmd_help.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>

extern command_t commands[];
extern const int commands_count;

// ==== /cmd_help command ====

int cmd_help(int argc, char *argv[],
             long chat_id,
             char *resp, size_t size,
             response_type_t *resp_type) {
  
  (void)argc; (void)argv; (void)chat_id;

    if (resp_type) *resp_type = RESP_MARKDOWN;

    int written = snprintf(resp, size, "*📚 COMMANDS*\n\n");

    if (written < 0 || (size_t)written >= size)
        return -1;

    size_t used = written;

    const char *current_category = NULL;

    for (int i = 0; i < commands_count; i++) {

        // 🛡️ защита от underflow
        if (used >= size - 1)
            break;

        if (!commands[i].category)
            continue;

        // 🔥 новая категория
        if (!current_category ||
            strcmp(current_category, commands[i].category) != 0) {

            int written = snprintf(resp + used, size - used,
                "%s*%s*\n",
                current_category ? "\n" : "",
                commands[i].category);

            if (written < 0 || (size_t)written >= size - used)
                break;

            used += written;
            current_category = commands[i].category;
          
        }

        const char *name = commands[i].name;
        
        // 👉 обычные команды
        int written = snprintf(resp + used, size - used,
            "%s\n",
            name);

        if (written < 0 || (size_t)written >= size - used)
            break;

        used += written;
    }

    LOG_STATE(LOG_DEBUG, "help: building command list");
    LOG_STATE(LOG_INFO, "help generated (len=%zu)", used);
    
    return 0;
}
