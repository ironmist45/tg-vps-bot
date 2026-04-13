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
#define TG_LIMIT 4096

static char g_token[128];
static char g_base_url[512];

// 🔥 защита от повторной обработки (reboot loop фикс)
static long g_last_processed_update = 0;

// ===== discard callback =====

static size_t discard_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)ptr;
    (void)userdata;
    return size * nmemb;
}

// ===== escape MarkdownV2 =====

static void escape_markdown(const char *src, char *dst, size_t size) {
    const char *special = "_[]()~`>#+-=|{}.!";

    size_t j = 0;

    for (size_t i = 0; src[i] != '\0' && j + 2 < size; i++) {
        if (strchr(special, src[i])) {
            dst[j++] = '\\';
        }
        dst[j++] = src[i];
    }

    dst[j] = '\0';
}

// ===== truncate =====

static void truncate_message(char *text) {
    size_t len = strlen(text);

    if (len >= TG_LIMIT) {
        text[TG_LIMIT - 20] = '\0';
        strcat(text, "\n...\n[truncated]");
    }
}

// ===== curl buffer =====

struct memory {
    char *data;
    size_t size;
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t real_size = size * nmemb;

    struct memory *mem = (struct memory *)userp;

    char *ptr = realloc(mem->data, mem->size + real_size + 1);
    if (!ptr) return 0;

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, real_size);
    mem->size += real_size;
    mem->data[mem->size] = 0;

    return real_size;
}

// ===== CURL SETUP (единая точка) =====

static void setup_curl(CURL *curl) {
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
}

// ===== init =====

int telegram_init(const char *token) {

    if (!token || strlen(token) == 0)
        return -1;

    strncpy(g_token, token, sizeof(g_token) - 1);
    g_token[sizeof(g_token) - 1] = '\0';

    snprintf(g_base_url, sizeof(g_base_url),
             "https://api.telegram.org/bot%s", g_token);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    log_msg(LOG_INFO, "Telegram initialized");

    return 0;
}

// ===== shutdown (🔥 важно для graceful reboot) =====

void telegram_shutdown(void) {
    curl_global_cleanup();
    log_msg(LOG_INFO, "Telegram shutdown");
}

// ===== send Markdown =====

int telegram_send_message(long chat_id, const char *text) {

    if (!text) return -1;

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    setup_curl(curl);

    char url[URL_MAX];
    snprintf(url, sizeof(url), "%s/sendMessage", g_base_url);

    char tmp[RESP_MAX];
    strncpy(tmp, text, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    truncate_message(tmp);

    char escaped[RESP_MAX];
    escape_markdown(tmp, escaped, sizeof(escaped));

    char post_fields[RESP_MAX];
    snprintf(post_fields, sizeof(post_fields),
             "chat_id=%ld&text=%s&parse_mode=MarkdownV2",
             chat_id, escaped);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_callback);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        log_msg(LOG_ERROR, "curl send error: %s", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    return 0;
}

// ===== send plain =====

int telegram_send_plain(long chat_id, const char *text) {

    if (!text) return -1;

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    setup_curl(curl);

    char url[URL_MAX];
    snprintf(url, sizeof(url), "%s/sendMessage", g_base_url);

    char tmp[RESP_MAX];
    strncpy(tmp, text, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    truncate_message(tmp);

    char post_fields[RESP_MAX];
    snprintf(post_fields, sizeof(post_fields),
             "chat_id=%ld&text=%s",
             chat_id, tmp);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_callback);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        log_msg(LOG_ERROR, "curl send error: %s", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    return 0;
}

// ===== poll =====

int telegram_poll() {

    static long last_update_id = 0;

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    setup_curl(curl);

    char url[URL_MAX];

    snprintf(url, sizeof(url),
             "%s/getUpdates?timeout=25&offset=%ld",
             g_base_url, last_update_id);

    struct memory chunk = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        log_msg(LOG_ERROR, "curl poll error: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        free(chunk.data);
        return -1;
    }

    cJSON *json = cJSON_Parse(chunk.data);
    if (!json) {
        log_msg(LOG_ERROR, "JSON parse error");
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
            if (!update_id) continue;

            long uid = (long)update_id->valuedouble;

            // 🔥 CRITICAL FIX: не обрабатывать повторно
            if (uid <= g_last_processed_update)
                continue;

            g_last_processed_update = uid;
            last_update_id = uid + 1;

            cJSON *message = cJSON_GetObjectItem(update, "message");
            if (!message) continue;

            cJSON *chat = cJSON_GetObjectItem(message, "chat");
            cJSON *chat_id = cJSON_GetObjectItem(chat, "id");
            cJSON *text = cJSON_GetObjectItem(message, "text");

            if (!chat_id || !text || !cJSON_IsString(text))
                continue;

            long cid = (long)chat_id->valuedouble;
            const char *msg_text = text->valuestring;

            if (!security_is_allowed_chat(cid))
                continue;

            if (security_validate_text(msg_text) != 0)
                continue;

            char response[RESP_MAX];

            if (commands_handle(msg_text, cid, response, sizeof(response)) == 0) {

                if (strncmp(msg_text, "/logs", 5) == 0) {
                    telegram_send_plain(cid, response);
                } else {
                    telegram_send_message(cid, response);
                }
            }
        }
    }

    cJSON_Delete(json);
    free(chunk.data);
    curl_easy_cleanup(curl);

    return 0;
}
