/**
 * tg-bot - Telegram bot for system administration
 * telegram.c - Public Telegram API and orchestration
 * MIT License - Copyright (c) 2026
 */

#include "telegram.h"
#include "telegram_http.h"
#include "telegram_parser.h"
#include "telegram_poll.h"
#include "telegram_offset.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>

#define RESP_MAX 8192

static char g_token[128];

int telegram_init(const char *token) {
    if (!token || strlen(token) == 0) return -1;
    strncpy(g_token, token, sizeof(g_token) - 1);
    g_token[sizeof(g_token) - 1] = '\0';
    return telegram_http_init(token);
}

void telegram_shutdown(void) {
    telegram_http_shutdown();
}

int telegram_send_message(long chat_id, const char *text) {
    if (!text) return -1;
    
    char tmp[RESP_MAX];
    strncpy(tmp, text, sizeof(tmp) - 1);
    telegram_truncate_message(tmp);
    
    char escaped[RESP_MAX];
    telegram_escape_markdown(tmp, escaped, sizeof(escaped));
    
    char post_fields[RESP_MAX];
    snprintf(post_fields, sizeof(post_fields), "chat_id=%ld&text=%s&parse_mode=MarkdownV2", chat_id, escaped);
    
    return telegram_http_request("sendMessage", post_fields, 0, NULL, NULL);
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
// telegram_get_poll_id() уже реализована в telegram_poll.c
