/**
 * tg-bot - Telegram bot for system administration
 * telegram_poll.c - Long polling implementation
 * MIT License - Copyright (c) 2026
 */

#include "telegram.h"
#include "telegram_poll.h"
#include "telegram_http.h"
#include "telegram_parser.h"
#include "telegram_offset.h"
#include "lifecycle.h"
#include "logger.h"
#include "commands.h"
#include "security.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define URL_MAX   1024
#define RESP_MAX  8192

static long g_last_processed_update = 0;
static int g_offset_counter = 0;
static unsigned short g_poll_cycle = 0;
static unsigned short g_current_poll_id = 0;

void telegram_poll_init(void) {
    LOG_NET(LOG_INFO, "Telegram poll module initialized");
}

unsigned short telegram_get_poll_id(void) {
    return g_current_poll_id;
}

int telegram_poll() {
    static long last_update_id = -1;

    g_poll_cycle++;
    unsigned short poll_id = g_poll_cycle;
    g_current_poll_id = poll_id;

    if (last_update_id == -1) {
        last_update_id = telegram_offset_load();
        telegram_offset_save(last_update_id);
        LOG_NET(LOG_INFO, "Starting poll from offset: %ld", last_update_id);
    }

    char url[URL_MAX];
    snprintf(url, sizeof(url), "getUpdates?timeout=25&offset=%ld", last_update_id);
    LOG_NET(LOG_DEBUG, "poll=%04x request (offset=%ld)", poll_id, last_update_id);

    char *raw_data = NULL;
    size_t raw_size = 0;
    int rc = telegram_http_request(url, "", 1, &raw_data, &raw_size);
    
    if (rc != 0) {
        LOG_NET(LOG_ERROR, "poll=%04x HTTP request failed", poll_id);
        return -1;
    }

    if (!raw_data || raw_size == 0) {
        LOG_NET(LOG_DEBUG, "poll=%04x empty response", poll_id);
        return 0;
    }

    telegram_update_t *updates = NULL;
    int count = 0;
    if (telegram_parse_updates(raw_data, raw_size, poll_id, &updates, &count) != 0) {
        free(raw_data);
        return -1;
    }
    free(raw_data);

    for (int i = 0; i < count; i++) {
        telegram_update_t *u = &updates[i];
        if (u->update_id <= g_last_processed_update) continue;
        
        g_last_processed_update = u->update_id;
        last_update_id = u->update_id + 1;
        telegram_offset_save(last_update_id);

        if (++g_offset_counter >= 5) {
            LOG_NET(LOG_DEBUG, "saving offset: %ld", last_update_id);
            telegram_offset_save(last_update_id);
            g_offset_counter = 0;
        }

        LOG_NET(LOG_INFO, "poll=%04x req=%04x upd=%ld incoming: chat_id=%ld len=%zu text=%.64s",
                g_current_poll_id, u->req_id, u->update_id, u->chat_id, strlen(u->text), u->text);

        if (!security_is_allowed_chat(u->chat_id)) {
            LOG_NET(LOG_WARN, "poll=%04x req=%04x ACCESS CHECK: chat_id=%ld cmd=%s result=DENIED",
                    g_current_poll_id, u->req_id, u->chat_id, u->text);
            continue;
        }
        if (security_validate_text(u->text) != 0) continue;

        char response[RESP_MAX];
        response_type_t resp_type;
        struct timespec req_start, req_end;
        clock_gettime(CLOCK_MONOTONIC, &req_start);

        if (commands_handle(u->text, u->chat_id, u->msg_date, u->user_id, u->username, u->req_id,
                            response, sizeof(response), &resp_type) == 0) {

            if (strncmp(u->text, "/logs", 5) == 0 || strncmp(u->text, "/fail2ban", 9) == 0) {
                telegram_send_plain(u->chat_id, response);
            } else {
                telegram_send_message(u->chat_id, response);
            }

            clock_gettime(CLOCK_MONOTONIC, &req_end);
            long req_ms = elapsed_ms(req_start, req_end);
            LOG_NET(LOG_INFO, "poll=%04x req=%04x done: %ld ms (resp=%zu)",
                    g_current_poll_id, u->req_id, req_ms, strlen(response));
        }
    }
    
    for (int i = 0; i < count; i++) {
        if (updates[i].text) free((void*)updates[i].text);
        if (updates[i].username) free((void*)updates[i].username);
    }
    free(updates);
    return 0;
}
