#include "telegram.h"
#include "logger.h"
#include "commands.h"
#include "security.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <cjson/cJSON.h>

#define URL_MAX 1024
#define RESP_MAX 8192

static char g_token[128];
static char g_base_url[512];

// ===== curl buffer =====

struct memory {
    char *data;
    size_t size;
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t real_size = size * nmemb;

    struct memory *mem = (struct memory *)userp;

    char *ptr = realloc(mem->data, mem->size + real_size + 1);
    if (!ptr)
        return 0;

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, real_size);
    mem->size += real_size;
    mem->data[mem->size] = 0;

    return real_size;
}

// ===== init =====

int telegram_init(const char *token) {

    if (!token || strlen(token) == 0)
        return -1;

    strncpy(g_token, token, sizeof(g_token) - 1);

    snprintf(g_base_url, sizeof(g_base_url),
             "https://api.telegram.org/bot%s", g_token);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    log_msg(LOG_INFO, "Telegram initialized");

    return 0;
}

// ===== send message =====

int telegram_send_message(long chat_id, const char *text) {

    if (!text) return -1;

    CURL *curl = curl_easy_init();
    if (!curl)
        return -1;

    char url[URL_MAX];

    if (strlen(g_base_url) + sizeof("/sendMessage") >= sizeof(url)) {
        log_msg(LOG_ERROR, "URL buffer too small (sendMessage)");
        curl_easy_cleanup(curl);
        return -1;
    }

    snprintf(url, sizeof(url), "%s/sendMessage", g_base_url);

    struct memory chunk = {0};

    char post_fields[RESP_MAX];

    snprintf(post_fields, sizeof(post_fields),
             "chat_id=%ld&text=%s",
             chat_id, text);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        log_msg(LOG_ERROR, "curl error: %s", curl_easy_strerror(res));
    }

    free(chunk.data);
    curl_easy_cleanup(curl);

    return 0;
}

// ===== poll =====

int telegram_poll() {

    static long last_update_id = 0;

    CURL *curl = curl_easy_init();
    if (!curl)
        return -1;

    char url[URL_MAX];

    if (strlen(g_base_url) + 64 >= sizeof(url)) {
        log_msg(LOG_ERROR, "URL buffer too small (getUpdates)");
        curl_easy_cleanup(curl);
        return -1;
    }

    snprintf(url, sizeof(url),
             "%s/getUpdates?timeout=30&offset=%ld",
             g_base_url, last_update_id);

    struct memory chunk = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        log_msg(LOG_ERROR, "curl error: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        free(chunk.data);
        return -1;
    }

    // ===== parse JSON =====

    cJSON *json = cJSON_Parse(chunk.data);
    if (!json) {
        log_msg(LOG_ERROR, "Failed to parse JSON");
        curl_easy_cleanup(curl);
        free(chunk.data);
        return -1;
    }

    cJSON *result = cJSON_GetObjectItem(json, "result");

    if (cJSON_IsArray(result)) {

        int count = cJSON_GetArraySize(result);

        for (int i = 0; i < count; i++) {

            cJSON *update = cJSON_GetArrayItem(result, i);

            cJSON *update_id = cJSON_GetObjectItem(update, "update_id");
            if (update_id)
                last_update_id = update_id->valuedouble + 1;

            cJSON *message = cJSON_GetObjectItem(update, "message");
            if (!message) continue;

            cJSON *chat = cJSON_GetObjectItem(message, "chat");
            cJSON *chat_id = cJSON_GetObjectItem(chat, "id");

            cJSON *text = cJSON_GetObjectItem(message, "text");

            if (!chat_id || !text || !cJSON_IsString(text))
                continue;

            long cid = (long)chat_id->valuedouble;
            const char *msg_text = text->valuestring;

            // 🔐 security check
            if (!security_is_allowed_chat(cid))
                continue;

            if (security_validate_text(msg_text) != 0)
                continue;

            char response[RESP_MAX];

            // 🔥 ВОТ ГЛАВНОЕ ИЗМЕНЕНИЕ
            if commands_handle(msg_text, cid, response, sizeof(response)); == 0) {
                telegram_send_message(cid, response);
            }
        }
    }

    cJSON_Delete(json);
    free(chunk.data);
    curl_easy_cleanup(curl);

    return 0;
}
