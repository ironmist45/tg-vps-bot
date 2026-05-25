/**
 * tg-bot - Telegram bot for system administration
 * telegram_parser.h - JSON parsing and message formatting API
 * MIT License - Copyright (c) 2026 ironmist45
 */
#ifndef TELEGRAM_PARSER_H
#define TELEGRAM_PARSER_H

#include <stddef.h>
#include <time.h>

/**
 * Parsed Telegram update.
 *
 * Either text or file_id is set — never both.
 *   text != NULL  → text message, handle via commands_handle()
 *   file_id != NULL → document/file, handle via upload module
 *
 * All const char* fields are heap-allocated (strdup) and must be
 * freed by the caller after use.
 */
typedef struct {
    long           update_id;
    unsigned short req_id;
    long           chat_id;
    int            user_id;
    const char    *username;    /* Telegram username, may be NULL           */
    time_t         msg_date;

    /* Text message fields */
    const char    *text;        /* Message text, NULL if this is a file     */

    /* Document/file fields — NULL if this is a text message */
    const char    *file_id;     /* Telegram file_id for getFile API         */
    const char    *file_name;   /* Original filename provided by sender     */
    int            file_size;   /* File size in bytes (0 if unknown)        */
} telegram_update_t;

/**
 * Initialize parser module
 */
void telegram_parser_init(void);

/**
 * Parse getUpdates response and extract updates
 *
 * Each update contains either a text message or a document.
 * Check update->file_id != NULL to detect incoming files.
 *
 * @param raw_data   Raw JSON response from Telegram API
 * @param data_size  Size of raw_data in bytes
 * @param poll_id    Current poll cycle ID for log correlation
 * @param updates    Output: heap-allocated array of updates (caller must free)
 * @param count      Output: number of updates parsed
 * @return           0 on success, -1 on error
 */
int telegram_parse_updates(const char *raw_data, size_t data_size,
                           unsigned short poll_id,
                           telegram_update_t **updates, int *count);

/**
 * Escape special characters for Telegram MarkdownV2
 *
 * @param src   Source string
 * @param dst   Destination buffer
 * @param size  Size of destination buffer
 */
void telegram_escape_markdown(const char *src, char *dst, size_t size);

/**
 * Truncate message to Telegram's 4096 character limit
 *
 * @param text  Message text (modified in-place, buffer must be >= TG_LIMIT bytes)
 */
void telegram_truncate_message(char *text);

#endif // TELEGRAM_PARSER_H
