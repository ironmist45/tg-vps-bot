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
#include <unistd.h>

#define RESP_MAX 4096

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
    
    LOG_NET(LOG_INFO, "telegram_send_message: START");
    
    char tmp[RESP_MAX];
    strncpy(tmp, text, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    LOG_NET(LOG_INFO, "telegram_send_message: after strncpy, len=%zu", strlen(tmp));
    
    telegram_truncate_message(tmp);
    LOG_NET(LOG_INFO, "telegram_send_message: after truncate, len=%zu", strlen(tmp));
    
    char escaped[RESP_MAX];
    telegram_escape_markdown(tmp, escaped, sizeof(escaped));
    LOG_NET(LOG_INFO, "telegram_send_message: after escape, len=%zu", strlen(escaped));
    
    char post_fields[RESP_MAX];
    snprintf(post_fields, sizeof(post_fields), "chat_id=%ld&text=%s&parse_mode=MarkdownV2", chat_id, escaped);
    LOG_NET(LOG_INFO, "telegram_send_message: after snprintf, post_fields len=%zu", strlen(post_fields));
    
    LOG_NET(LOG_INFO, "telegram_send_message: calling telegram_http_request");
    int rc = telegram_http_request("sendMessage", post_fields, 0, NULL, NULL);
    LOG_NET(LOG_INFO, "telegram_send_message: rc=%d", rc);
    
    return rc;
}

int telegram_send_plain(long chat_id, const char *text) {
    if (!text) return -1;
    
    char tmp[RESP_MAX];
    strncpy(tmp, text, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    telegram_truncate_message(tmp);
    
    char post_fields[RESP_MAX];
    snprintf(post_fields, sizeof(post_fields), "chat_id=%ld&text=%s", chat_id, tmp);
    
    return telegram_http_request("sendMessage", post_fields, 0, NULL, NULL);
}

long telegram_get_last_offset(void) {
    return telegram_offset_get_last();
}

int telegram_poll_safe(int max_errors) {
    static int consecutive_errors = 0;
    
    int rc = telegram_poll();
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
