/**
 * tg-bot - Telegram bot for system administration
 * telegram_parser.h - JSON parsing and message formatting API
 * MIT License - Copyright (c) 2026
 */

#ifndef TELEGRAM_PARSER_H
#define TELEGRAM_PARSER_H

#include <stddef.h>
#include <time.h>

typedef struct {
    long update_id;
    unsigned short req_id;
    long chat_id;
    int user_id;
    const char *username;
    time_t msg_date;
    const char *text;
} telegram_update_t;

/**
 * Parse getUpdates response and extract updates
 * @param raw_data Raw JSON response
 * @param data_size Size of raw_data
 * @param poll_id Current poll cycle ID for logging
 * @param updates Output array of updates (caller must free)
 * @param count Number of updates parsed
 * @return 0 on success, -1 on error
 */
int telegram_parse_updates(const char *raw_data, size_t data_size, unsigned short poll_id,
                           telegram_update_t **updates, int *count);

/**
 * Escape special characters for Telegram MarkdownV2
 * @param src Source string
 * @param dst Destination buffer
 * @param size Size of destination buffer
 */
void telegram_escape_markdown(const char *src, char *dst, size_t size);

/**
 * Truncate message to Telegram's 4096 character limit
 * @param text Message text (modified in-place)
 */
void telegram_truncate_message(char *text);

#endif // TELEGRAM_PARSER_H
