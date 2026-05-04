/**
 * tg-bot - Telegram bot for system administration
 * telegram_parser.c - JSON parsing and message formatting implementation
 * MIT License - Copyright (c) 2026 ironmist45
 */

#define _GNU_SOURCE

#include "telegram_parser.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <cjson/cJSON.h>

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
 * free_updates_partial - освободить частично заполненный массив обновлений.
 *
 * Вызывается из всех error-path'ов внутри telegram_parse_updates() чтобы
 * не допустить утечки strdup'd строк при OOM или других ошибках парсинга.
 * count — количество уже полностью заполненных элементов (parsed_count
 * на момент ошибки).  Текущий незавершённый слот (если text/username уже
 * выделены но parsed_count ещё не инкрементирован) передаётся отдельно
 * через current_slot: если >= 0, его поля тоже освобождаются.
 * ------------------------------------------------------------------------- */
static void free_updates_partial(telegram_update_t *arr, int count,
                                 int current_slot)
{
    if (!arr) return;

    /* Освобождаем полностью засчитанные элементы */
    for (int i = 0; i < count; i++) {
        free((void *)arr[i].text);
        free((void *)arr[i].username);
    }

    /* Освобождаем текущий незавершённый слот если он был начат */
    if (current_slot >= 0 && current_slot != count) {
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

    cJSON *json = cJSON_Parse(raw_data);
    if (!json) {
        LOG_NET(LOG_ERROR, "poll=%04x JSON parse error", poll_id);
        return -1;
    }

    cJSON *ok = cJSON_GetObjectItem(json, "ok");
    if (!cJSON_IsTrue(ok)) {
        cJSON *error_code   = cJSON_GetObjectItem(json, "error_code");
        cJSON *description  = cJSON_GetObjectItem(json, "description");
        LOG_NET(LOG_ERROR, "poll=%04x API error %d: %s", poll_id,
                error_code   ? error_code->valueint       : 0,
                description  ? description->valuestring   : "unknown");
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
        cJSON *update    = cJSON_GetArrayItem(result, i);
        const cJSON *update_id = cJSON_GetObjectItem(update, "update_id");
        if (!update_id) continue;

        long uid = (long)update_id->valuedouble;

        /* Инициализируем слот перед любыми strdup-вызовами.
         * calloc уже обнулил массив, но явная инициализация защищает
         * free_updates_partial от двойного free при повторном использовании
         * слота (которого сейчас нет, но defensive coding). */
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

        /* strdup text — обязательное поле, без него запись бесполезна */
        parsed[parsed_count].text = strdup(text->valuestring);
        if (!parsed[parsed_count].text) {
            LOG_NET(LOG_ERROR,
                    "poll=%04x strdup(text) failed at update %d (OOM)",
                    poll_id, i);
            /*
             * Текущий слот начат (chat_id заполнен, text = NULL).
             * Передаём parsed_count как current_slot — free_updates_partial
             * попробует free(NULL) для text что безопасно.
             */
            free_updates_partial(parsed, parsed_count, parsed_count);
            cJSON_Delete(json);
            return -1;
        }

        /* strdup username — опциональное поле, NULL допустим */
        cJSON *from = cJSON_GetObjectItem(message, "from");
        if (from) {
            const cJSON *user_id = cJSON_GetObjectItem(from, "id");
            cJSON *username = cJSON_GetObjectItem(from, "username");

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
                     * username некритичен — продолжаем без него.
                     * NULL в поле username корректно обрабатывается везде
                     * в коде (проверки "if (username)" повсюду).
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

void telegram_truncate_message(char *text) {
    size_t len = strlen(text);
    if (len >= TG_LIMIT && TG_LIMIT > 20) {
        text[TG_LIMIT - 20] = '\0';
        snprintf(text + strlen(text), TG_LIMIT - strlen(text), "\n...\n[truncated]");
    }
}
