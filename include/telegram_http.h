/**
 * tg-bot - Telegram bot for system administration
 * telegram_http.h - Low-level HTTP communication API
 * MIT License - Copyright (c) 2026
 */

#ifndef TELEGRAM_HTTP_H
#define TELEGRAM_HTTP_H

#include <stddef.h>

/**
 * Initialize HTTP module
 * @param token Telegram Bot API token
 * @return 0 on success, -1 on error
 */
int telegram_http_init(const char *token);

/**
 * Shutdown HTTP module and cleanup resources
 */
void telegram_http_shutdown(void);

/**
 * Perform HTTP request to Telegram API
 * @param method API method (e.g., "getUpdates", "sendMessage")
 * @param post_fields URL-encoded POST fields
 * @param need_response 1 to capture response, 0 to discard
 * @param out_data Output buffer for response (caller must free)
 * @param out_size Size of response
 * @return 0 on success, -1 on error
 */
int telegram_http_request(const char *method, const char *post_fields, int need_response,
                          char **out_data, size_t *out_size);

#endif // TELEGRAM_HTTP_H
