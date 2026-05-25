/**
 * tg-bot - Telegram bot for system administration
 * telegram_http.h - Low-level HTTP communication API
 * MIT License - Copyright (c) 2026 ironmist45
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
int telegram_http_request(const char *method, const char *post_fields,
                          int need_response,
                          char **out_data, size_t *out_size);

/**
 * Download a file from an arbitrary HTTPS URL and write it to a FILE*.
 *
 * Used by the upload module to fetch files from the Telegram file
 * download endpoint (https://api.telegram.org/file/bot<token>/<path>).
 * Reuses the same curl configuration (timeouts, keep-alive, etc.) as
 * telegram_http_request().
 *
 * @param url   Full HTTPS URL to download
 * @param fp    Open writable FILE* to write the response body into
 * @return      0 on success, -1 on curl error or NULL arguments
 */
int telegram_http_download_file(const char *url, void *fp);

#endif // TELEGRAM_HTTP_H
