/**
 * tg-bot - Telegram bot for system administration
 * upload.h - Telegram file upload handling
 * MIT License - Copyright (c) 2026 ironmist45
 */
#ifndef UPLOAD_H
#define UPLOAD_H

#include <stddef.h>

/*
 * Maximum length of a sanitized filename.
 * Filenames from Telegram are sanitized before use — only alphanumeric,
 * dot, hyphen and underscore characters are kept.
 */
#define UPLOAD_FILENAME_MAX 128

/*
 * Maximum length of a Telegram file_id string.
 */
#define UPLOAD_FILE_ID_MAX  256

/*
 * Maximum length of the full destination path:
 * upload_dir (256) + "/" + filename (128) = 385 + null = 386.
 * 512 provides comfortable headroom.
 */
#define UPLOAD_PATH_MAX     512

/**
 * Download a file sent to the bot and save it to the upload directory.
 *
 * Flow:
 *   1. Call getFile API with file_id → get file_path on Telegram servers
 *   2. Build download URL: https://api.telegram.org/file/bot<token>/<file_path>
 *   3. Download file via telegram_http_download_file() → write to disk
 *   4. Return sanitized filename and full destination path to caller
 *
 * The destination directory is taken from g_cfg.upload_dir.
 * If the directory does not exist the download will fail at fopen().
 *
 * @param file_id        Telegram file_id from the incoming document update
 * @param file_name      Original filename provided by sender (may be NULL)
 * @param req_id         Request ID for log correlation
 * @param out_path       Output: full path where file was saved
 * @param out_path_size  Size of out_path buffer
 * @return               0 on success, -1 on error
 */
int upload_receive_file(const char *file_id,
                        const char *file_name,
                        unsigned short req_id,
                        char *out_path,
                        size_t out_path_size);

/**
 * List files in the upload directory.
 *
 * Formats a Markdown response with filenames and sizes.
 * Shows "No files" if the directory is empty.
 *
 * @param buffer  Output buffer for Telegram response
 * @param size    Size of output buffer
 * @param req_id  Request ID for log correlation
 * @return        0 on success, -1 on error
 */
int upload_list_files(char *buffer, size_t size, unsigned short req_id);

/**
 * Format a file size in human-readable form (B / KB / MB).
 *
 * @param bytes  File size in bytes
 * @param buf    Output buffer
 * @param size   Buffer size
 */
void upload_format_size(long bytes, char *buf, size_t size);

#endif // UPLOAD_H
