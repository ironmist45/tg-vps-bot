/* tg-bot — telegram_http.c — Low-level HTTP communication. MIT License © 2026 ironmist45 */

#include "telegram_http.h"
#include "logger.h"
#include "telegram_timeouts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <curl/curl.h>

/*
 * Maximum URL length: base URL (~60) + method (~30) + params (~900) = ~1000.
 * 1024 provides comfortable headroom.
 */
#define URL_MAX 1024

/*
 * Base URL buffer: "https://api.telegram.org/bot" + token (46 chars) = ~76.
 * 512 provides comfortable headroom.
 */
#define BASE_URL_MAX 512

/* TCP keep-alive idle time before sending probes */
#define HTTP_TCP_KEEPIDLE_SEC  30L

/* Interval between keep-alive probes */
#define HTTP_TCP_KEEPINTVL_SEC 15L

/* DNS cache TTL — works with c-ares resolver */
#define HTTP_DNS_CACHE_SEC     300L

static char g_base_url[BASE_URL_MAX];

/*
 * http_chunk_t - growable buffer for libcurl write callback.
 * Accumulates response data across multiple callback invocations.
 */
typedef struct {
    char  *data;
    size_t size;
} http_chunk_t;

// cppcheck-suppress constParameterCallback
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    /* Guard against overflow: size * nmemb must not wrap size_t */
    if (size != 0 && nmemb > SIZE_MAX / size) {
        return 0;
    }

    size_t real_size = size * nmemb;
    http_chunk_t *mem = (http_chunk_t *)userp;

    char *ptr = realloc(mem->data, mem->size + real_size + 1);
    if (!ptr) {
        LOG_NET(LOG_ERROR, "realloc failed");
        free(mem->data);
        mem->data = NULL;
        mem->size = 0;
        return 0;   /* signals curl to abort the request */
    }

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, real_size);
    mem->size += real_size;
    mem->data[mem->size] = '\0';
    return real_size;
}

/*
 * write_file_callback - libcurl write callback that writes directly to a FILE*.
 * Used by telegram_http_download_file() to stream response body to disk
 * without buffering the entire file in memory.
 */
// cppcheck-suppress constParameterCallback
static size_t write_file_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    if (size != 0 && nmemb > SIZE_MAX / size)
        return 0;

    return fwrite(contents, size, nmemb, (FILE *)userp);
}

static void setup_curl(CURL *curl) {
    (void)curl_easy_setopt(curl, CURLOPT_TIMEOUT,           (long)TG_HTTP_TIMEOUT_SEC);
    (void)curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,    (long)TG_CONNECT_TIMEOUT_SEC);
    (void)curl_easy_setopt(curl, CURLOPT_NOSIGNAL,          1L);
    (void)curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE,     1L);
    (void)curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE,      HTTP_TCP_KEEPIDLE_SEC);
    (void)curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL,     HTTP_TCP_KEEPINTVL_SEC);
    (void)curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, HTTP_DNS_CACHE_SEC);
    (void)curl_easy_setopt(curl, CURLOPT_FORBID_REUSE,      0L);
    (void)curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT,     0L);
}

int telegram_http_init(const char *token) {
    if (!token || strlen(token) == 0) return -1;
    snprintf(g_base_url, sizeof(g_base_url),
             "https://api.telegram.org/bot%s", token);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    LOG_NET(LOG_INFO, "Telegram HTTP module initialized");
    return 0;
}

void telegram_http_shutdown(void) {
    curl_global_cleanup();
    LOG_NET(LOG_INFO, "Telegram HTTP module shutdown");
}

/**
 * Perform an HTTP request to the Telegram Bot API.
 *
 * @param method        API method name (e.g. "getUpdates", "sendMessage")
 * @param post_fields   URL-encoded POST body
 * @param need_response 1 — caller wants response data via out_data/out_size
 *                      0 — response is discarded after request
 * @param out_data      Output: allocated response buffer (caller must free)
 * @param out_size      Output: response size in bytes
 * @return              0 on success, -1 on curl error
 */
int telegram_http_request(const char *method, const char *post_fields,
                          int need_response,
                          char **out_data, size_t *out_size)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        LOG_NET(LOG_ERROR, "curl_easy_init failed for %s", method);
        return -1;
    }

    setup_curl(curl);

    char url[URL_MAX];
    int n = snprintf(url, sizeof(url), "%s/%s", g_base_url, method);
    if (n < 0 || (size_t)n >= sizeof(url)) {
        LOG_NET(LOG_ERROR, "URL truncated for method: %s", method);
        curl_easy_cleanup(curl);
        return -1;
    }

    http_chunk_t chunk = {0};

    (void)curl_easy_setopt(curl, CURLOPT_URL,          url);
    (void)curl_easy_setopt(curl, CURLOPT_POST,          1L);
    (void)curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    post_fields);
    (void)curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    (void)curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &chunk);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        LOG_NET(LOG_ERROR, "curl error on %s: %s", method, curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        free(chunk.data);
        return -1;
    }

    if (need_response && out_data) {
        *out_data = chunk.data;
        if (out_size) *out_size = chunk.size;
    } else {
        free(chunk.data);
        if (out_data) *out_data = NULL;
        if (out_size) *out_size = 0;
    }

    curl_easy_cleanup(curl);
    return 0;
}

/**
 * Download a file from an arbitrary HTTPS URL and write it to a FILE*.
 *
 * Uses a file write callback instead of the memory buffer used by
 * telegram_http_request() — no need to hold the entire file in memory.
 * Same curl configuration (timeouts, keep-alive) as other requests.
 *
 * @param url  Full HTTPS URL to fetch
 * @param fp   Open writable FILE* (caller opens/closes)
 * @return     0 on success, -1 on error
 */
int telegram_http_download_file(const char *url, void *fp) {
    if (!url || !fp) return -1;

    CURL *curl = curl_easy_init();
    if (!curl) {
        LOG_NET(LOG_ERROR, "curl_easy_init failed for download: %s", url);
        return -1;
    }

    setup_curl(curl);

    (void)curl_easy_setopt(curl, CURLOPT_URL,           url);
    (void)curl_easy_setopt(curl, CURLOPT_HTTPGET,       1L);
    (void)curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_callback);
    (void)curl_easy_setopt(curl, CURLOPT_WRITEDATA,     fp);
    /* Follow redirects — Telegram file downloads may redirect */
    (void)curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    (void)curl_easy_setopt(curl, CURLOPT_MAXREDIRS,      3L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        /* Log only the path component — full URL contains the bot TOKEN */
        const char *safe_url = strstr(url, "/file/bot");
        LOG_NET(LOG_ERROR, "download failed: %s — %s",
                safe_url ? safe_url : "(url masked)", curl_easy_strerror(res));
        return -1;
    }

    return 0;
}
