/* tg-bot — telegram_parser.c — JSON parsing and message formatting. MIT License © 2026 ironmist45 */

#define _GNU_SOURCE

#include "telegram_parser.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <cjson/cJSON.h>

/*
 * Telegram message length limit in bytes.
 * telegram_truncate_message() assumes callers provide a buffer of at
 * least TG_LIMIT bytes — see function doc comment.
 */
#define TG_LIMIT 4096

static unsigned short make_req_id(long update_uid) {
    uint32_t x = (uint32_t)update_uid;
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return (unsigned short)(x & 0xFFFF);
}

void telegram_parser_init(void) {
    LOG_NET(LOG_INFO, "Telegram parser module initialized");
}

/* -------------------------------------------------------------------------
 * free_updates_partial - release a partially populated updates array.
 *
 * Called from all error paths inside telegram_parse_updates() to prevent
 * leaking strdup'd strings on OOM or parse errors.
 *
 * count        — number of fully committed elements to free.
 * current_slot — index of the in-progress slot whose fields may be
 *                partially allocated; pass -1 if no slot was started.
 *                free(NULL) is safe, so partially initialized slots are
 *                released unconditionally.
 * ------------------------------------------------------------------------- */
static void free_updates_partial(telegram_update_t *arr, int count,
                                 int current_slot)
{
    if (!arr) return;

    /* Free fully committed elements */
    for (int i = 0; i < count; i++) {
        free((void *)arr[i].text);
        free((void *)arr[i].username);
    }

    /* Free the in-progress slot if one was started */
    if (current_slot >= 0) {
        free((void *)arr[current_slot].text);
        free((void *)arr[current_slot].username);
    }

    free(arr);
}

int telegram_parse_updates(const char *raw_data, size_t data_size,
                           unsigned short poll_id,
                           telegram_update_t **updates, int *count)
{
    *updates = NULL;
    *count   = 0;

    /* Validate inputs — data_size == 0 means no data to parse */
    if (!raw_data || data_size == 0)
        return 0;

    cJSON *json = cJSON_Parse(raw_data);
    if (!json) {
        LOG_NET(LOG_ERROR, "poll=%04x JSON parse error", poll_id);
        return -1;
    }

    cJSON *ok = cJSON_GetObjectItem(json, "ok");
    if (!cJSON_IsTrue(ok)) {
        cJSON *error_code  = cJSON_GetObjectItem(json, "error_code");
        cJSON *description = cJSON_GetObjectItem(json, "description");
        LOG_NET(LOG_ERROR, "poll=%04x API error %d: %s", poll_id,
                error_code  ? error_code->valueint     : 0,
                description ? description->valuestring : "unknown");
        cJSON_Delete(json);
        return -1;
    }

    cJSON *result = cJSON_GetObjectItem(json, "result");
    if (!cJSON_IsArray(result)) {
        cJSON_Delete(json);
        return 0;
    }

    int arr_size = cJSON_GetArraySize(result);
    if (arr_size == 0) {
        cJSON_Delete(json);
        return 0;
    }

    telegram_update_t *parsed = calloc(arr_size, sizeof(telegram_update_t));
    if (!parsed) {
        LOG_NET(LOG_ERROR, "poll=%04x failed to allocate updates array", poll_id);
        cJSON_Delete(json);
        return -1;
    }

    int parsed_count = 0;

    for (int i = 0; i < arr_size; i++) {
        cJSON *update          = cJSON_GetArrayItem(result, i);
        const cJSON *update_id = cJSON_GetObjectItem(update, "update_id");
        if (!update_id) continue;

        long uid = (long)update_id->valuedouble;

        /*
         * Initialise the slot before any strdup calls.
         * calloc already zeroed the array, but explicit initialisation
         * protects free_updates_partial from double-free if the slot
         * were ever reused (defensive coding).
         */
        parsed[parsed_count].update_id = uid;
        parsed[parsed_count].req_id    = make_req_id(uid);
        parsed[parsed_count].text      = NULL;
        parsed[parsed_count].username  = NULL;

        cJSON *message = cJSON_GetObjectItem(update, "message");
        if (!message) continue;

        cJSON *chat = cJSON_GetObjectItem(message, "chat");
        cJSON *text = cJSON_GetObjectItem(message, "text");
        if (!chat || !text || !cJSON_IsString(text)) continue;

        const cJSON *chat_id = cJSON_GetObjectItem(chat, "id");
        if (!chat_id) continue;

        parsed[parsed_count].chat_id = (long)chat_id->valuedouble;

        /* strdup text — required field; without it the entry is useless */
        parsed[parsed_count].text = strdup(text->valuestring);
        if (!parsed[parsed_count].text) {
            LOG_NET(LOG_ERROR,
                    "poll=%04x strdup(text) failed at update %d (OOM)",
                    poll_id, i);
            /*
             * Current slot is in progress (chat_id set, text = NULL).
             * Pass parsed_count as current_slot — free(NULL) is safe.
             */
            free_updates_partial(parsed, parsed_count, parsed_count);
            cJSON_Delete(json);
            return -1;
        }

        /* strdup username — optional field, NULL is acceptable */
        cJSON *from = cJSON_GetObjectItem(message, "from");
        if (from) {
            const cJSON *user_id = cJSON_GetObjectItem(from, "id");
            cJSON *username      = cJSON_GetObjectItem(from, "username");

            if (user_id)
                parsed[parsed_count].user_id = (int)user_id->valuedouble;

            if (username && cJSON_IsString(username)) {
                parsed[parsed_count].username = strdup(username->valuestring);
                if (!parsed[parsed_count].username) {
                    LOG_NET(LOG_WARN,
                            "poll=%04x strdup(username) failed at update %d "
                            "(OOM) — continuing without username",
                            poll_id, i);
                    /*
                     * username is non-critical — proceed without it.
                     * NULL in the username field is handled correctly
                     * everywhere (all callers check "if (username)").
                     */
                }
            }
        }

        cJSON *date = cJSON_GetObjectItem(message, "date");
        if (date && cJSON_IsNumber(date))
            parsed[parsed_count].msg_date = (time_t)date->valuedouble;

        parsed_count++;
    }

    cJSON_Delete(json);
    *updates = parsed;
    *count   = parsed_count;
    return 0;
}

void telegram_escape_markdown(const char *src, char *dst, size_t size) {
    const char *special = "_[]()~>#+-=|{}.!";
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 2 < size; i++) {
        if (strchr(special, src[i])) dst[j++] = '\\';
        dst[j++] = src[i];
    }
    dst[j] = '\0';
}

/**
 * Truncate message text to Telegram's length limit.
 *
 * IMPORTANT: caller must ensure text buffer is at least TG_LIMIT bytes.
 * This function does not receive the buffer size — it assumes the caller
 * has allocated enough space. Violating this contract causes an OOB write.
 */
void telegram_truncate_message(char *text) {
    size_t len = strlen(text);
    if (len >= TG_LIMIT && TG_LIMIT > 20) {
        text[TG_LIMIT - 20] = '\0';
        snprintf(text + strlen(text), 20, "\n...\n[truncated]");
    }
}
