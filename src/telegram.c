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

#define OFFSET_FILE "/var/lib/tg-bot/offset.dat"
#define OFFSET_TMP  "/var/lib/tg-bot/offset.tmp"

static char g_token[128];
static char g_base_url[512];

// защита от дублей (runtime)
static long g_last_processed_update = 0;

// уменьшение записи offset
static int g_offset_counter = 0;

// ===== offset persistence =====

static long load_offset(void) {
    FILE *f = fopen(OFFSET_FILE, "r");
    if (!f) return 0;

    long offset = 0;
    if (fscanf(f, "%ld", &offset) != 1)
        offset = 0;

    fclose(f);

    LOG_STATE(LOG_INFO, "Loaded offset: %ld", offset);
    return offset;
}

static void save_offset(long offset) {
    FILE *f = fopen(OFFSET_TMP, "w");
    if (!f) {
        LOG_STATE(LOG_WARN, "Failed to save offset (tmp)");
        return;
    }

    fprintf(f, "%ld\n", offset);
    fclose(f);

    if (rename(OFFSET_TMP, OFFSET_FILE) != 0) {
        LOG_STATE(LOG_WARN, "Failed to rename offset file");
    }
}

// ===== discard callback =====

static size_t discard_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)ptr;
    (void)userdata;
    return size * nmemb;
}

// ===== escape MarkdownV2 =====

static void escape_markdown(const char *src, char *dst, size_t size) {
    const char *special = "_[]()~>#+-=|{}.!";

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

    if (len >= TG_LIMIT && TG_LIMIT > 20) {
        text[TG_LIMIT - 20] = '\0';
        snprintf(text + strlen(text),
                 TG_LIMIT - strlen(text),
                 "\n...\n[truncated]");
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

// ===== CURL SETUP =====

static void setup_curl(CURL *curl) {
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 35L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);
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

    LOG_NET(LOG_INFO, "Telegram initialized");

    return 0;
}

// ===== shutdown =====

void telegram_shutdown(void) {
    curl_global_cleanup();
    LOG_NET(LOG_INFO, "Telegram shutdown");
}

// ===== send Markdown =====

int telegram_send_message(long chat_id, const char *text) {

    if (!text) return -1;
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    setup_curl(curl);

    char url[URL_MAX];
    snprintf(url, sizeof(url), "%s/sendMessage", g_base_url);
    LOG_NET(LOG_DEBUG, "send message request");
    LOG_NET(LOG_DEBUG, "send url: %s", url);

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
      
    LOG_NET(LOG_DEBUG,
            "send md: chat_id=%ld len=%zu",
            chat_id, strlen(text));
    
    CURLcode res = curl_easy_perform(curl);
    
    LOG_NET(LOG_DEBUG,
            "send curl result: %d (%s)",
            res,
            curl_easy_strerror(res));

    if (res != CURLE_OK) {
        LOG_NET(LOG_ERROR, "curl send error: %s", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    return 0;
}

// ===== send plain =====

int telegram_send_plain(long chat_id, const char *text) {

    if (!text) return -1;
    
    LOG_NET(LOG_DEBUG,
            "send plain: chat_id=%ld len=%zu",
            chat_id, strlen(text));
    
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

    LOG_NET(LOG_DEBUG,
            "send plain curl result: %d (%s)",
            res,
            curl_easy_strerror(res));

    if (res != CURLE_OK) {
        LOG_NET(LOG_ERROR, "curl send error: %s", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    return 0;
}

// ===== poll =====

int telegram_poll() {

    static long last_update_id = -1;

    if (last_update_id == -1) {
        last_update_id = load_offset();
        LOG_NET(LOG_INFO, "Starting poll from offset: %ld", last_update_id);
    }

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    setup_curl(curl);

    char url[URL_MAX];
    snprintf(url, sizeof(url),
             "%s/getUpdates?timeout=25&offset=%ld",
             g_base_url, last_update_id);

    LOG_NET(LOG_DEBUG, "poll request (offset=%ld)", last_update_id);

    struct memory chunk = {0};

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);

    CURLcode res = curl_easy_perform(curl);

    LOG_NET(LOG_DEBUG,
            "poll curl result: %d (%s)",
            res,
            curl_easy_strerror(res));

    if (res == CURLE_OPERATION_TIMEDOUT) {
        curl_easy_cleanup(curl);
        free(chunk.data);
        return 0;
    }

    if (res != CURLE_OK) {
        LOG_NET(LOG_ERROR, "curl poll error: %s", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        free(chunk.data);
        return -1;
    }

    if (!chunk.data || chunk.size == 0) {
        LOG_NET(LOG_DEBUG, "Empty response from Telegram");
        curl_easy_cleanup(curl);
        free(chunk.data);
        return 0;
    }

    cJSON *json = cJSON_Parse(chunk.data);
    if (!json) {
    LOG_NET(LOG_ERROR,
            "JSON parse error, raw=%.512s",
            chunk.data ? chunk.data : "NULL");
        curl_easy_cleanup(curl);
        free(chunk.data);
        return -1;
    }

    cJSON *ok = cJSON_GetObjectItem(json, "ok");
    if (!cJSON_IsTrue(ok)) {
    LOG_NET(LOG_ERROR,
            "Telegram API not ok: %.512s",
            chunk.data);
        cJSON_Delete(json);
        curl_easy_cleanup(curl);
        free(chunk.data);
        return -1;
    }

    cJSON *result = cJSON_GetObjectItem(json, "result");

    if (cJSON_IsArray(result)) {

        int count = cJSON_GetArraySize(result);
        LOG_NET(LOG_DEBUG, "updates received: %d", count);

        for (int i = 0; i < count; i++) {

            cJSON *update = cJSON_GetArrayItem(result, i);

            cJSON *update_id = cJSON_GetObjectItem(update, "update_id");
            if (!update_id) {
                LOG_NET(LOG_WARN, "update without update_id");
                continue;
            }

            long update_uid = (long)update_id->valuedouble;
            LOG_NET(LOG_DEBUG, "update_id=%ld", uid);

            if (update_uid <= g_last_processed_update)
                continue;

            g_last_processed_update = uid;
            last_update_id = uid + 1;

            if (++g_offset_counter >= 5) {
                LOG_NET(LOG_DEBUG, "saving offset: %ld", last_update_id);
                save_offset(last_update_id);
                g_offset_counter = 0;
            }

            cJSON *message = cJSON_GetObjectItem(update, "message");
            if (!message) {
                LOG_NET(LOG_DEBUG, "skipping update: no message");
                continue;
            }

            cJSON *from = cJSON_GetObjectItem(message, "from");
            cJSON *user_id_json = from ? cJSON_GetObjectItem(from, "id") : NULL;
            cJSON *username_json = from ? cJSON_GetObjectItem(from, "username") : NULL;

            // 🔥 и сразу парсинг
            int uid = 0;
            const char *uname = NULL;

            if (user_id_json) {
                uid = (int)user_id_json->valuedouble;
            }

            if (username_json && cJSON_IsString(username_json)) {
                uname = username_json->valuestring;
            }

            cJSON *date = cJSON_GetObjectItem(message, "date");

            time_t msg_date = 0;

            if (date && cJSON_IsNumber(date)) {
                msg_date = (time_t)date->valuedouble;
            }

            cJSON *chat = cJSON_GetObjectItem(message, "chat");
            if (!chat) {
                LOG_NET(LOG_DEBUG, "skipping update: no chat");
                continue;
            }

            cJSON *chat_id = cJSON_GetObjectItem(chat, "id");
            cJSON *text = cJSON_GetObjectItem(message, "text");

            if (!chat_id || !text || !cJSON_IsString(text)) {
                LOG_NET(LOG_DEBUG, "skipping update: no text");
                continue;
            }

            long cid = (long)chat_id->valuedouble;
            const char *msg_text = text->valuestring;
            LOG_NET(LOG_INFO,
                    "incoming: chat_id=%ld len=%zu text=%.64s",
                    cid, strlen(msg_text), msg_text);
            LOG_NET(LOG_DEBUG,
                    "user: id=%d username=%s",
                    uid,
                    uname ? uname : "NULL");

            if (!security_is_allowed_chat(cid)) {
                LOG_NET(LOG_WARN,
                        "ACCESS DENIED: chat_id=%ld len=%zu text=%.32s",
                        cid, strlen(msg_text), msg_text);
                continue;
            }

            if (security_validate_text(msg_text) != 0)
                continue;

            char response[RESP_MAX];

            response_type_t resp_type;

            if (commands_handle(msg_text, cid, msg_date,
                                uid, uname,
                                response, sizeof(response), &resp_type) == 0) {
                
                LOG_NET(LOG_DEBUG,
                        "response ready: %zu bytes",
                        strlen(response));

                if (strncmp(msg_text, "/logs", 5) == 0 ||
                    strncmp(msg_text, "/fail2ban", 9) == 0) {
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
