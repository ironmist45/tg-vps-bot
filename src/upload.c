/* tg-bot — upload.c — Telegram file receiving and saving. MIT License © 2026 ironmist45 */

/*
 * Handles incoming Telegram document updates:
 *   1. Calls getFile API to resolve file_id → file_path on Telegram servers
 *   2. Builds download URL and fetches file via telegram_http_download_file()
 *   3. Saves to g_cfg.upload_dir with a sanitized filename
 *   4. Provides upload_list_files() for /files command
 */

#include "upload.h"
#include "config.h"
#include "logger.h"
#include "telegram_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cjson/cJSON.h>

/*
 * Telegram file download base URL.
 * Full URL: TG_FILE_BASE + token + "/" + file_path
 */
#define TG_FILE_BASE "https://api.telegram.org/file/bot"

/*
 * Maximum length of file_path returned by getFile API.
 * Typical: "documents/file_123456789.conf" — 256 is safe headroom.
 */
#define FILE_PATH_MAX 256

/*
 * Maximum length of the getFile API response buffer.
 * getFile returns a small JSON object — 1024 bytes is more than enough.
 */
#define GETFILE_BUF_MAX 1024

/*
 * Maximum size of the full download URL:
 * TG_FILE_BASE (~36) + token (~46) + "/" + file_path (256) = ~340.
 * 512 provides comfortable headroom.
 */
#define DOWNLOAD_URL_MAX 512

/*
 * Format string for human-readable file sizes in upload_list_files().
 */
#define SIZE_BUF_MAX 32

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

/**
 * Sanitize a filename — keep only alphanumeric, dot, hyphen, underscore.
 * Spaces are replaced with underscore.
 *
 * Prevents path traversal (no slashes), shell injection, and hidden
 * files / ".." (names starting with '.' are rejected entirely).
 *
 * Examples:
 *   "hello world.pdf" → "hello_world.pdf"
 *   ".hidden"         → rejected
 *   "../etc/passwd"   → rejected (starts with '.' after sanitization)
 *   "config.conf"     → "config.conf"
 *
 * @param src  Original filename from Telegram (may be NULL)
 * @param dst  Output buffer for sanitized name
 * @param size Size of dst buffer
 * @return     0 on success, -1 if result is empty, NULL, or starts with '.'
 */
static int sanitize_filename(const char *src, char *dst, size_t size) {
    if (!src || src[0] == '\0') return -1;

    size_t j = 0;

    for (size_t i = 0; src[i] && j < size - 1; i++) {
        char c = src[i];
        if (c == ' ') {
            dst[j++] = '_';  /* replace spaces with underscore */
        } else if (isalnum((unsigned char)c) ||
                   c == '.' || c == '-' || c == '_') {
            dst[j++] = c;
        }
        /* All other characters are silently dropped */
    }

    dst[j] = '\0';

    /* Reject empty result */
    if (j == 0)
        return -1;

    /* Reject names starting with '.' — covers hidden files and ".." */
    if (dst[0] == '.')
        return -1;

    return 0;
}

/**
 * Format a file size in human-readable form.
 *
 * @param bytes  File size in bytes
 * @param buf    Output buffer
 * @param size   Buffer size
 */
void upload_format_size(long bytes, char *buf, size_t size) {
    if (bytes < 1024)
        snprintf(buf, size, "%ld B", bytes);
    else if (bytes < 1024 * 1024)
        snprintf(buf, size, "%.1f KB", (double)bytes / 1024.0);
    else
        snprintf(buf, size, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
}

/**
 * Call getFile API and extract file_path from the response.
 *
 * getFile response:
 *   {"ok":true,"result":{"file_id":"...","file_path":"documents/file_123.conf",...}}
 *
 * @param file_id        Telegram file_id
 * @param req_id         Request ID for log correlation
 * @param out_file_path  Output buffer for file_path
 * @param out_size       Size of out_file_path buffer
 * @return               0 on success, -1 on error
 */
static int get_file_path(const char *file_id, unsigned short req_id,
                         char *out_file_path, size_t out_size)
{
    /* Build POST fields for getFile */
    char post_fields[UPLOAD_FILE_ID_MAX + 16];
    int n = snprintf(post_fields, sizeof(post_fields),
                     "file_id=%s", file_id);
    if (n < 0 || (size_t)n >= sizeof(post_fields)) {
        LOG_CMD(LOG_ERROR, "req=%04x getFile: file_id too long", req_id);
        return -1;
    }

    char *resp     = NULL;
    size_t resp_sz = 0;

    if (telegram_http_request("getFile", post_fields, 1, &resp, &resp_sz) != 0) {
        LOG_CMD(LOG_ERROR, "req=%04x getFile HTTP request failed", req_id);
        return -1;
    }

    if (!resp || resp_sz == 0) {
        LOG_CMD(LOG_ERROR, "req=%04x getFile empty response", req_id);
        free(resp);
        return -1;
    }

    /* Parse JSON response */
    cJSON *json = cJSON_Parse(resp);
    free(resp);

    if (!json) {
        LOG_CMD(LOG_ERROR, "req=%04x getFile JSON parse error", req_id);
        return -1;
    }

    cJSON *ok = cJSON_GetObjectItem(json, "ok");
    if (!cJSON_IsTrue(ok)) {
        cJSON *desc = cJSON_GetObjectItem(json, "description");
        LOG_CMD(LOG_ERROR, "req=%04x getFile API error: %s", req_id,
                desc ? desc->valuestring : "unknown");
        cJSON_Delete(json);
        return -1;
    }

    cJSON *result    = cJSON_GetObjectItem(json, "result");
    cJSON *file_path = result ? cJSON_GetObjectItem(result, "file_path") : NULL;

    if (!file_path || !cJSON_IsString(file_path)) {
        LOG_CMD(LOG_ERROR, "req=%04x getFile: missing file_path in response", req_id);
        cJSON_Delete(json);
        return -1;
    }

    if (snprintf(out_file_path, out_size, "%s", file_path->valuestring) < 0) {
        cJSON_Delete(json);
        return -1;
    }

    LOG_CMD(LOG_DEBUG, "req=%04x getFile: file_path=%s", req_id, out_file_path);

    cJSON_Delete(json);
    return 0;
}

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Download a file sent to the bot and save it to the upload directory.
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
                        size_t out_path_size)
{
    if (!file_id || file_id[0] == '\0') {
        LOG_CMD(LOG_ERROR, "req=%04x upload: empty file_id", req_id);
        return -1;
    }

    LOG_CMD(LOG_INFO, "req=%04x upload: receiving name=%s file_id=%.16s",
            req_id, file_name ? file_name : "(none)", file_id);

    /* Step 1 — resolve file_id → file_path via getFile API */
    char file_path[FILE_PATH_MAX] = {0};
    if (get_file_path(file_id, req_id, file_path, sizeof(file_path)) != 0)
        return -1;

    /* Step 2 — sanitize filename */
    char safe_name[UPLOAD_FILENAME_MAX] = {0};

    if (sanitize_filename(file_name, safe_name, sizeof(safe_name)) != 0) {
        /*
         * Filename missing or contains only unsafe characters.
         * Fall back to the last component of file_path:
         *   "documents/file_123456789.conf" → "file_123456789.conf"
         */
        const char *slash = strrchr(file_path, '/');
        const char *base  = slash ? slash + 1 : file_path;

        if (sanitize_filename(base, safe_name, sizeof(safe_name)) != 0) {
            /* Last resort: use a generic name */
            snprintf(safe_name, sizeof(safe_name), "upload_%04x", req_id);
        }

        LOG_CMD(LOG_DEBUG,
                "req=%04x upload: original name unusable, using '%s'",
                req_id, safe_name);
    }

    /* Step 3 — build destination path */
    int n = snprintf(out_path, out_path_size, "%s/%s",
                     g_cfg.upload_dir, safe_name);
    if (n < 0 || (size_t)n >= out_path_size) {
        LOG_CMD(LOG_ERROR, "req=%04x upload: destination path too long", req_id);
        return -1;
    }

    /* Step 4 — build download URL */
    char download_url[DOWNLOAD_URL_MAX];
    n = snprintf(download_url, sizeof(download_url),
                 "%s%s/%s", TG_FILE_BASE, g_cfg.token, file_path);
    if (n < 0 || (size_t)n >= sizeof(download_url)) {
        LOG_CMD(LOG_ERROR, "req=%04x upload: download URL too long", req_id);
        return -1;
    }

    /* Step 5 — open destination file */
    FILE *fp = fopen(out_path, "wb");
    if (!fp) {
        LOG_CMD(LOG_ERROR, "req=%04x upload: cannot open '%s' for writing",
                req_id, out_path);
        return -1;
    }

    /* Step 6 — stream file from Telegram to disk */
    LOG_CMD(LOG_DEBUG, "req=%04x upload: downloading to '%s'", req_id, out_path);

    if (telegram_http_download_file(download_url, fp) != 0) {
        LOG_CMD(LOG_ERROR, "req=%04x upload: download failed", req_id);
        fclose(fp);
        /* Remove incomplete file */
        remove(out_path);
        return -1;
    }

    fclose(fp);

    /* Step 7 — log result with file size */
    struct stat st;
    long saved_bytes = 0;
    if (stat(out_path, &st) == 0)
        saved_bytes = (long)st.st_size;

    char size_str[SIZE_BUF_MAX];
    upload_format_size(saved_bytes, size_str, sizeof(size_str));

    return 0;
}

/**
 * List files in the upload directory.
 *
 * Output format (Markdown):
 *   📁 *UPLOADS*
 *
 *   `filename.conf` — 1.2 KB
 *   `archive.tar.gz` — 4.5 MB
 *
 *   Total: 3 files
 *
 * @param buffer  Output buffer for Telegram response
 * @param size    Size of output buffer
 * @param req_id  Request ID for log correlation
 * @return        0 on success, -1 on error
 */
int upload_list_files(char *buffer, size_t size, unsigned short req_id) {
    if (!buffer || size == 0) return -1;

    DIR *dir = opendir(g_cfg.upload_dir);
    if (!dir) {
        LOG_CMD(LOG_ERROR, "req=%04x files: cannot open upload dir '%s'",
                req_id, g_cfg.upload_dir);
        snprintf(buffer, size, "❌ Cannot open upload directory");
        return -1;
    }

    /* Write header */
    size_t used = 0;
    int n = snprintf(buffer, size, "📁 *UPLOADS*\n\n");
    if (n > 0 && (size_t)n < size)
        used += (size_t)n;

    int file_count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (entry->d_name[0] == '.')
            continue;

        /* Build full path to stat the file */
        char full_path[UPLOAD_PATH_MAX];
        n = snprintf(full_path, sizeof(full_path),
                     "%s/%s", g_cfg.upload_dir, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(full_path))
            continue;

        struct stat st;
        if (stat(full_path, &st) != 0)
            continue;

        /* Skip subdirectories — only list regular files */
        if (!S_ISREG(st.st_mode))
            continue;

        char size_str[SIZE_BUF_MAX];
        upload_format_size((long)st.st_size, size_str, sizeof(size_str));

        n = snprintf(buffer + used, size - used,
                     "`%s` — %s\n", entry->d_name, size_str);
        if (n < 0 || used + (size_t)n >= size)
            break;

        used += (size_t)n;
        file_count++;
    }

    closedir(dir);

    if (file_count == 0) {
        snprintf(buffer + used, size - used, "_No files_");
    } else {
        snprintf(buffer + used, size - used,
                 "\nTotal: %d file%s",
                 file_count, file_count == 1 ? "" : "s");
    }

    LOG_CMD(LOG_INFO, "req=%04x files: listed %d files", req_id, file_count);
    return 0;
}
