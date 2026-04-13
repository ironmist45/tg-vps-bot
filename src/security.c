#include "security.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

// 🔐 секрет (лучше вынести в config)
#define SECRET_KEY 0x5F3759DF

static int g_token_ttl = 60;

// ===== init =====

void security_init(void) {
    log_msg(LOG_INFO, "Security initialized (stateless mode)");
}

void security_set_allowed_chat(long chat_id) {
    (void)chat_id; // не используется в stateless версии
}

void security_set_token_ttl(int ttl) {
    if (ttl > 0 && ttl <= 3600)
        g_token_ttl = ttl;
}

// ===== access =====

int security_is_allowed_chat(long chat_id) {
    (void)chat_id;
    return 1; // проверка делается снаружи (или можно вернуть обратно)
}

int security_validate_text(const char *text) {
    if (!text) return -1;
    if (strlen(text) > 1024) return -1;
    return 0;
}

// ===== hash =====

static uint32_t hash_token(long chat_id, uint32_t ts) {
    uint32_t h = (uint32_t)chat_id;

    h ^= ts;
    h ^= SECRET_KEY;

    // немного перемешивания (xorshift)
    h ^= (h << 13);
    h ^= (h >> 17);
    h ^= (h << 5);

    return h;
}

// ===== generate =====

int security_generate_reboot_token(long chat_id) {

    uint32_t ts = (uint32_t)time(NULL);

    uint32_t h = hash_token(chat_id, ts);

    // кодируем timestamp в старшие биты
    uint32_t token = ((ts & 0xFFFF) << 16) | (h & 0xFFFF);

    log_msg(LOG_INFO,
        "Generated token: chat_id=%ld ts=%u token=%u",
        chat_id, ts, token);

    return (int)token;
}

// ===== validate =====

int security_validate_reboot_token(long chat_id, int input_token) {

    uint32_t token = (uint32_t)input_token;

    uint32_t ts_part = (token >> 16) & 0xFFFF;
    uint32_t hash_part = token & 0xFFFF;

    time_t now = time(NULL);

    for (int i = 0; i <= g_token_ttl; i++) {

        uint32_t ts = (uint32_t)(now - i);

        if ((ts & 0xFFFF) != ts_part)
            continue;

        uint32_t expected_hash = hash_token(chat_id, ts) & 0xFFFF;

        if (expected_hash == hash_part) {
            log_msg(LOG_INFO,
                "Token accepted: chat_id=%ld token=%d (age=%d sec)",
                chat_id, input_token, i);
            return 0;
        }
    }

    log_msg(LOG_WARN,
        "Invalid token: chat_id=%ld token=%d",
        chat_id, input_token);

    return -1;
}
