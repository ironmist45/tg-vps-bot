/**
 * tg-bot - Telegram bot for system administration
 * telegram.c - Public Telegram API and orchestration
 * MIT License - Copyright (c) 2026
 */

#include "lifecycle.h"
#include "telegram.h"
#include "telegram_http.h"
#include "telegram_parser.h"
#include "telegram_poll.h"
#include "telegram_offset.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>   // для sleep()

#define RESP_MAX 32768

static char g_token[128];

int telegram_init(const char *token) {
    if (!token || strlen(token) == 0) return -1;
    strncpy(g_token, token, sizeof(g_token) - 1);
    g_token[sizeof(g_token) - 1] = '\0';
    
    if (telegram_http_init(token) != 0) return -1;
    telegram_parser_init();
    telegram_offset_init();
    telegram_poll_init();
    
    LOG_NET(LOG_INFO, "Telegram main module initialized");
    return 0;
}

void telegram_shutdown(void) {
    telegram_http_shutdown();
}

int telegram_send_message(long chat_id, const char *text) {
    if (!text) return -1;

    LOG_NET(LOG_DEBUG, "telegram_send_message: chat_id=%ld, text=%.50s", chat_id, text);
    
    char tmp[RESP_MAX];
    strncpy(tmp, text, sizeof(tmp) - 1);
    telegram_truncate_message(tmp);
    
    char escaped[RESP_MAX];
    telegram_escape_markdown(tmp, escaped, sizeof(escaped));
    
    char post_fields[RESP_MAX];
    snprintf(post_fields, sizeof(post_fields), "chat_id=%ld&text=%s&parse_mode=MarkdownV2", chat_id, escaped);
    
    LOG_NET(LOG_DEBUG, "telegram_send_message: calling telegram_http_request");
    int rc = telegram_http_request("sendMessage", post_fields, 0, NULL, NULL);
    LOG_NET(LOG_DEBUG, "telegram_send_message: rc=%d", rc);
    
    return rc;
}

int telegram_send_plain(long chat_id, const char *text) {
    if (!text) return -1;
    
    char tmp[RESP_MAX];
    strncpy(tmp, text, sizeof(tmp) - 1);
    telegram_truncate_message(tmp);
    
    char post_fields[RESP_MAX];
    snprintf(post_fields, sizeof(post_fields), "chat_id=%ld&text=%s", chat_id, tmp);
    
    return telegram_http_request("sendMessage", post_fields, 0, NULL, NULL);
}

// Эти функции просто проксируют вызовы в соответствующие модули
long telegram_get_last_offset(void) {
    return telegram_offset_get_last();
}

// ============================================================================
// SAFE POLLING WITH ERROR HANDLING
// ============================================================================

int telegram_poll_safe(int max_errors) {
    static int consecutive_errors = 0;
    
    int rc = telegram_poll();
    LOG_NET(LOG_DEBUG, "telegram_poll_safe: rc=%d", rc);
    if (rc != 0) {
        if (lifecycle_shutdown_requested()) {
            return -1;
        }
        
        consecutive_errors++;
        LOG_NET(LOG_WARN, "Polling error (rc=%d, attempt=%d/%d)", 
                rc, consecutive_errors, max_errors);
        
        if (consecutive_errors >= max_errors) {
            LOG_SYS(LOG_ERROR, "Too many consecutive polling errors, exiting");
            return -1;
        }
        sleep(5);
        return 0;
    }
    
    consecutive_errors = 0;
    return 0;
}
