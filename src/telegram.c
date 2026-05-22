/* tg-bot — telegram.c — Public Telegram API and orchestration. MIT License © 2026 ironmist45 */

#include "telegram.h"
#include "telegram_http.h"
#include "telegram_parser.h"
#include "telegram_poll.h"
#include "telegram_offset.h"
#include "logger.h"
#include "metrics.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * Working buffer size for message text and POST fields.
 * Must be large enough to hold the escaped message plus URL-encoded
 * chat_id, parse_mode, and other POST parameters (~50 bytes overhead).
 */
#define MSG_BUF_MAX  4096
#define POST_BUF_MAX (MSG_BUF_MAX + 128)

// ============================================================================
// INITIALIZATION
// ============================================================================

int telegram_init(const char *token) {
    if (!token || strlen(token) == 0) return -1;

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

// ============================================================================
// MESSAGE SENDING
// ============================================================================

/**
 * Send a Markdown-formatted message to a chat.
 *
 * Text is truncated to Telegram limits, then escaped for MarkdownV2.
 * Returns -1 if post_fields would be truncated (message too long after
 * escaping) to avoid sending garbled output.
 */
int telegram_send_message(long chat_id, const char *text) {
    if (!text) return -1;

    char tmp[MSG_BUF_MAX];
    if (safe_copy(tmp, sizeof(tmp), text) != 0) {
        LOG_NET(LOG_WARN, "send_message: text truncated for chat_id=%ld", chat_id);
        tmp[sizeof(tmp) - 1] = '\0';
    }

    telegram_truncate_message(tmp);

    char escaped[MSG_BUF_MAX];
    telegram_escape_markdown(tmp, escaped, sizeof(escaped));

    char post_fields[POST_BUF_MAX];
    int n = snprintf(post_fields, sizeof(post_fields),
                     "chat_id=%ld&text=%s&parse_mode=MarkdownV2",
                     chat_id, escaped);

    if (n < 0 || (size_t)n >= sizeof(post_fields)) {
        LOG_NET(LOG_WARN, "send_message: post_fields truncated for chat_id=%ld",
                chat_id);
        return -1;
    }

    return telegram_http_request("sendMessage", post_fields, 0, NULL, NULL);
}

/**
 * Send a plain-text message to a chat (no Markdown formatting).
 *
 * Text is truncated to Telegram limits but not escaped.
 * Returns -1 if post_fields would be truncated.
 */
int telegram_send_plain(long chat_id, const char *text) {
    if (!text) return -1;

    char tmp[MSG_BUF_MAX];
    if (safe_copy(tmp, sizeof(tmp), text) != 0) {
        LOG_NET(LOG_WARN, "send_plain: text truncated for chat_id=%ld", chat_id);
        tmp[sizeof(tmp) - 1] = '\0';
    }

    telegram_truncate_message(tmp);

    char post_fields[POST_BUF_MAX];
    int n = snprintf(post_fields, sizeof(post_fields),
                     "chat_id=%ld&text=%s", chat_id, tmp);

    if (n < 0 || (size_t)n >= sizeof(post_fields)) {
        LOG_NET(LOG_WARN, "send_plain: post_fields truncated for chat_id=%ld",
                chat_id);
        return -1;
    }

    return telegram_http_request("sendMessage", post_fields, 0, NULL, NULL);
}

// ============================================================================
// OFFSET
// ============================================================================

long telegram_get_last_offset(void) {
    return telegram_offset_get_last();
}
