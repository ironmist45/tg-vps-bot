#include "commands.h"
#include "services.h"
#include "utils.h"

#include <stdio.h>

// ==== COMMANDS: /services /users /logs ====
//
// ==== /services command ====

int cmd_services(int argc, char *argv[],
                     long chat_id,
                     char *resp, size_t size,
                     response_type_t *resp_type) {
    (void)argc;

    if (resp_type) *resp_type = RESP_MARKDOWN;

    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    }

    REQUIRE_ACCESS(chat_id, argv[0], resp, size);
  
    if (services_get_status(resp, size) != 0) {
        snprintf(resp, size, "⚠️ Failed to get services");
        return -1;
    }
  
return 0;
  
}
