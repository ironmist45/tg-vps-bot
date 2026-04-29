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
    // libcurl может вызвать callback с userp=NULL при завершении запроса
    if (!userp) {
        return size * nmemb;
    }
    
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
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);
    curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 300L);    // DNS-кэш на 5 минут (работает с c-ares)
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0L);           // Разрешаем libcurl переиспользовать существующее TCP-соединение (keep-alive)
    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 0L);          // Не принуждаем создавать новое соединение — libcurl может использовать уже открытое, если оно доступно
}

int telegram_http_init(const char *token) {
    if (!token || strlen(token) == 0) return -1;
    snprintf(g_base_url, sizeof(g_base_url), "https://api.telegram.org/bot%s", token);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    LOG_NET(LOG_INFO, "Telegram HTTP module initialized");
    return 0;
}

void telegram_http_shutdown(void) {
    curl_global_cleanup();
    LOG_NET(LOG_INFO, "Telegram HTTP module shutdown");
}

int telegram_http_request(const char *method, const char *post_fields, int need_response,
                          char **out_data, size_t *out_size) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        LOG_NET(LOG_ERROR, "curl_easy_init failed for %s", method);
        return -1;
    }
    
    setup_curl(curl);

    char url[URL_MAX];
    snprintf(url, sizeof(url), "%s/%s", g_base_url, method);

    struct memory chunk = {0};
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);

    CURLcode res = curl_easy_perform(curl);
    
    if (res != CURLE_OK) {
        LOG_NET(LOG_ERROR, "curl error on %s: %s", method, curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        if (chunk.data) {
            free(chunk.data);
            chunk.data = NULL;
        }
        return -1;
    }

    if (need_response) {
        if (out_data) *out_data = chunk.data;
        if (out_size) *out_size = chunk.size;
    } else {
        if (chunk.data) {
            free(chunk.data);
            chunk.data = NULL;
        }
        if (out_data) *out_data = NULL;
        if (out_size) *out_size = 0;
    }

    curl_easy_cleanup(curl);
    return 0;
}
