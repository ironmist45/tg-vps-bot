/**
 * tg-bot - Telegram bot for system administration
 * 
 * cmd_control.c - System control commands (/reboot)
 * 
 * MIT License - Copyright (c) 2026
 */

#include "commands.h"
#include "utils.h"
#include "security.h"
#include "lifecycle.h"

#include <stdio.h>
#include <signal.h>

// ============================================================================
// REBOOT COMMANDS
// ============================================================================

/**
 * /reboot - Request system reboot (generates confirmation token)
 */
int cmd_reboot(int argc, char *argv[],
               long chat_id,
               char *resp, size_t size,
               response_type_t *resp_type) {
    (void)argc;

    if (resp_type) *resp_type = RESP_MARKDOWN;
    
    if (validate_command(argv, resp, size) != 0)
        return -1;

    int token = security_generate_reboot_token(chat_id);

    int written = snprintf(resp, size,
        "⚠️ Confirm reboot:\n`/reboot_confirm %d`",
        token);
    if (written < 0 || (size_t)written >= size)
        return -1;

    return 0;
}

/**
 * /reboot_confirm <token> - Confirm and execute reboot
 */
int cmd_reboot_confirm(int argc, char *argv[],
                       long chat_id,
                       char *resp, size_t size,
                       response_type_t *resp_type) {
    
    if (resp_type) *resp_type = RESP_MARKDOWN;
  
    if (validate_command(argv, resp, size) != 0)
        return -1;
    
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

    // Request reboot via lifecycle module
    lifecycle_request_shutdown(2, chat_id);

    if (snprintf(resp, size, "♻️ Rebooting...") >= (int)size)
        return -1;
  
    return 0;
}
