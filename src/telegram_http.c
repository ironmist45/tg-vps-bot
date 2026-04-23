/**
 * tg-bot - Telegram bot for system administration
 * telegram_http.c - Low-level HTTP communication implementation
 * MIT License - Copyright (c) 2026
 */

#include "telegram_http.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#define URL_MAX 1024

static char g_base_url[512];

struct memory {
    char *data;
    size_t size;
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t real_size = size * nmemb;
    struct memory *mem = (struct memory *)userp;

    char *ptr = realloc(mem->data, mem->size + real_size + 1);
    if (!ptr) {
        LOG_NET(LOG_ERROR, "realloc failed");
        free(mem->data);
        mem->data = NULL;
        mem->size = 0;
        return 0;
    }

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, real_size);
    mem->size += real_size;
    mem->data[mem->size] = '\0';
    return real_size;
}

static void setup_curl(CURL *curl) {
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 35L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);
}

int telegram_http_init(const char *token) {
    if (!token || strlen(token) == 0) return -1;
    snprintf(g_base_url, sizeof(g_base_url), "https://api.telegram.org/bot%s", token);
    LOG_NET(LOG_INFO, "Telegram HTTP module initialized");
    return 0;
}

void telegram_http_shutdown(void) {
    LOG_NET(LOG_INFO, "Telegram HTTP module shutdown");
}

int telegram_http_request(const char *method, const char *post_fields, int need_response,
                          char **out_data, size_t *out_size) {
    LOG_NET(LOG_INFO, "telegram_http_request: ENTRY, method=%s", method);
    
    CURL *curl = curl_easy_init();
    LOG_NET(LOG_INFO, "telegram_http_request: after curl_easy_init, curl=%p", (void*)curl);
    
    if (!curl) {
        LOG_NET(LOG_ERROR, "curl_easy_init failed");
        return -1;
    }

    setup_curl(curl);

    char url[URL_MAX];
    snprintf(url, sizeof(url), "%s/%s", g_base_url, method);
    LOG_NET(LOG_INFO, "telegram_http_request: url=%.100s", url);

    struct memory chunk;
    chunk.data = NULL;
    chunk.size = 0;
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);

    LOG_NET(LOG_DEBUG, "telegram_http_request: calling curl_easy_perform");
    CURLcode res = curl_easy_perform(curl);
    LOG_NET(LOG_DEBUG, "telegram_http_request: curl_easy_perform done, res=%d", res);
    
    if (res != CURLE_OK) {
        LOG_NET(LOG_ERROR, "curl error on %s: %s", method, curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        if (chunk.data) free(chunk.data);
        return -1;
    }

    if (need_response) {
        *out_data = chunk.data;
        *out_size = chunk.size;
        LOG_NET(LOG_DEBUG, "telegram_http_request: returning data, size=%zu", chunk.size);
    } else {
        LOG_NET(LOG_DEBUG, "telegram_http_request: discarding data, size=%zu", chunk.size);
        if (chunk.data) {
            free(chunk.data);
            chunk.data = NULL;
        }
        *out_data = NULL;
        *out_size = 0;
    }

    curl_easy_cleanup(curl);
    LOG_NET(LOG_DEBUG, "telegram_http_request: success");
    return 0;
}
