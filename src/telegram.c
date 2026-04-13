#include "telegram.h"
#include "logger.h"
#include "commands.h"
#include "utils.h"
#include "security.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>
#include <cjson/cJSON.h>

#define URL_MAX 1024
#define TG_MAX_MSG 4000

static char base_url[URL_MAX];
static long allowed_chat_id = 0;
static long last_update_id = 0;

// ===== buffer =====

struct memory {
    char *data;
    size_t size;
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    struct memory *mem = (struct memory *)userp;

    char *ptr = realloc(mem->data, mem->size + total + 1);
    if (!ptr) return 0;

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, total);
    mem->size += total;
    mem->data[mem->size] = 0;

    return total;
}

// ===== init =====

int telegram_init(const config_t *cfg) {
    snprintf(base_url, sizeof(base_url),
             "https://api.telegram.org/bot%s", cfg->token);

    allowed_chat_id = cfg->chat_id;

    security_init(cfg->chat_id);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    log_msg(LOG_INFO, "Telegram initialized");
    return 0;
}

void telegram_cleanup() {
    curl_global_cleanup();
}

// ===== HTTP =====

static int http_get(const char *url, struct memory *chunk) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    chunk->data = malloc(1);
    chunk->size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, chunk);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        log_msg(LOG_ERROR, "curl GET failed: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        free(chunk->data);
        return -1;
    }

    curl_easy_cleanup(curl);
    return 0;
}

static int http_post(const char *url, const char *post_fields) {
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        log_msg(LOG_ERROR, "curl POST failed: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return -1;
    }

    curl_easy_cleanup(curl);
    return 0;
}

// ===== sendMessage =====

int telegram_send_message(const char *text) {
    char url[URL_MAX];
    char post[4096];
    char encoded[2048];

    snprintf(url, sizeof(url), "%s/sendMessage", base_url);

    if (url_encode(text, encoded, sizeof(encoded)) != 0) {
        log_msg(LOG_ERROR, "URL encode failed");
        return -1;
    }

    snprintf(post, sizeof(post),
             "chat_id=%ld&text=%s",
             allowed_chat_id, encoded);

    return http_post(url, post);
}

// ===== LONG MESSAGE =====

int telegram_send_long_message(const char *text) {

    if (!text) return -1;

    size_t len = strlen(text);
    size_t offset = 0;

    while (offset < len) {

        size_t chunk_size = TG_MAX_MSG;

        if (offset + chunk_size > len) {
            chunk_size = len - offset;
        }

        size_t end = offset + chunk_size;

        // стараемся резать по строкам
        if (end < len) {
            for (size_t i = end; i > offset; i--) {
                if (text[i] == '\n') {
                    end = i;
                    break;
                }
            }
        }

        size_t final_size = end - offset;

        char chunk[TG_MAX_MSG + 1];
        memcpy(chunk, text + offset, final_size);
        chunk[final_size] = '\0';

        if (telegram_send_message(chunk) != 0) {
            log_msg(LOG_ERROR, "Failed to send chunk");
            return -1;
        }

        offset = end;

        if (text[offset] == '\n') {
            offset++;
        }

        // чуть притормаживаем (анти-спам Telegram)
        usleep(200000); // 200 ms
    }

    return 0;
}

// ===== обработка update =====

static void handle_update(cJSON *update) {
    cJSON *update_id = cJSON_GetObjectItem(update, "update_id");
    if (update_id) {
        last_update_id = update_id->valuedouble + 1;
    }

    cJSON *message = cJSON_GetObjectItem(update, "message");
    if (!message) return;

    cJSON *chat = cJSON_GetObjectItem(message, "chat");
    cJSON *text = cJSON_GetObjectItem(message, "text");

    if (!chat || !text) return;

    cJSON *chat_id = cJSON_GetObjectItem(chat, "id");
    if (!chat_id) return;

    long cid = chat_id->valuedouble;

    // ===== SECURITY =====
    if (!security_is_allowed_chat(cid)) {
        return;
    }

    const char *msg = text->valuestring;

    if (security_validate_text(msg) != 0) {
        log_msg(LOG_WARN, "Blocked invalid message");
        return;
    }

    log_msg(LOG_INFO, "Received: %s", msg);

    char response[4096];

    commands_handle(msg, response, sizeof(response));

    // 👇 ВАЖНО: теперь всегда используем long version
    telegram_send_long_message(response);
}

// ===== polling =====

int telegram_poll() {
    char url[URL_MAX];

    snprintf(url, sizeof(url),
             "%s/getUpdates?timeout=30&offset=%ld",
             base_url, last_update_id);

    struct memory chunk;

    if (http_get(url, &chunk) != 0) {
        return -1;
    }

    cJSON *json = cJSON_Parse(chunk.data);
    if (!json) {
        log_msg(LOG_ERROR, "JSON parse error");
        free(chunk.data);
        return -1;
    }

    cJSON *result = cJSON_GetObjectItem(json, "result");
    if (!result || !cJSON_IsArray(result)) {
        cJSON_Delete(json);
        free(chunk.data);
        return -1;
    }

    int size = cJSON_GetArraySize(result);

    for (int i = 0; i < size; i++) {
        cJSON *update = cJSON_GetArrayItem(result, i);
        handle_update(update);
    }

    cJSON_Delete(json);
    free(chunk.data);

    return 0;
}
