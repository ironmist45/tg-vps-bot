#include "commands.h"
#include "utils.h"
#include "security.h"

#include <stdio.h>
#include <signal.h>

// extern globals
extern volatile sig_atomic_t g_shutdown_requested;
extern long g_reboot_requested_by;

// ===== REBOOT: /reboot and /reboot_confirm commands =====
// ==== /reboot command ====
int cmd_reboot(int argc, char *argv[],
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

    int token = security_generate_reboot_token(chat_id);

    int written = snprintf(resp, size,
        "⚠️ Confirm reboot:\n`/reboot_confirm %d`",
        token);
    if (written < 0 || (size_t)written >= size)
        return -1;

    return 0;
}

// ==== /reboot_confirm command ====

int cmd_reboot_confirm(int argc, char *argv[],
                       long chat_id,
                       char *resp, size_t size,
                       response_type_t *resp_type) {
    
    if (resp_type) *resp_type = RESP_MARKDOWN;
  
    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    }

    REQUIRE_ACCESS(chat_id, argv[0], resp, size);
    
    if (argc < 2) {
        snprintf(resp, size, "Usage: /reboot_confirm <token>");
        return -1;
    }
   
    int token;

    if (parse_int(argv[1], &token) != 0) {
        snprintf(resp, size, "Invalid token format");
        return -1;
    }

    if (security_validate_reboot_token(chat_id, token) != 0) {
        snprintf(resp, size, "❌ Invalid or expired token");
        return -1;
    }

    g_reboot_requested_by = chat_id;   // 👈 КТО запросил ребут
    g_shutdown_requested = 2;

    if (snprintf(resp, size, "♻️ Rebooting...") >= (int)size)
        return -1;
  
  return 0;

}
