#include "security.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

// ⚠️ SECURITY NOTE:
// This is NOT a cryptographic secret.
// Tokens are protected by runtime salt (generated at startup).
// Purpose:
//   - prevent accidental execution
//   - add confirmation step for critical actions
// Real security is enforced by chat_id validation.
#define TOKEN_SALT 0x5F3759DF

static long g_allowed_chat_id = 0;
static int g_token_ttl = 60;
// 🔐 runtime salt (меняется при каждом запуске)
static unsigned int g_runtime_salt = 0;

// ===== INIT =====

void security_init(void) {
    // 🔐 генерируем runtime salt
    g_runtime_salt = (unsigned int)rand();

    log_msg(LOG_INFO, "Security initialized (stateless tokens)");
}

// ===== CONFIG =====

void security_set_allowed_chat(long chat_id) {
    g_allowed_chat_id = chat_id;
}

void security_set_token_ttl(int ttl) {
    if (ttl > 0 && ttl <= 3600)
        g_token_ttl = ttl;
}

// ===== ACCESS =====

int security_is_allowed_chat(long chat_id) {
    return chat_id == g_allowed_chat_id;
}

int security_validate_text(const char *text) {
    if (!text) return -1;

    size_t len = strlen(text);

    if (len == 0 || len > 1024)
        return -1;

    return 0;
}

// ===== TOKEN CORE =====

// 🔥 улучшенный hash (без отрицательных значений)
static int make_token(long chat_id, int ts) {

    unsigned int x = (unsigned int)(chat_id ^ ts ^ TOKEN_SALT ^ g_runtime_salt);

    // немного "перемешаем" биты
    x ^= (x >> 16);
    x *= 0x45d9f3b;
    x ^= (x >> 16);

    return (int)(x % 1000000); // всегда 0..999999
}

// ===== GENERATE =====

int security_generate_reboot_token(long chat_id) {

    int ts = (int)time(NULL);

    int token = make_token(chat_id, ts);

    int final_token = (ts % 1000000) ^ token;

    log_msg(LOG_INFO,
        "Generated token: chat_id=%ld ts=%d token=%06d",
        chat_id, ts, final_token);

    return final_token;
}

// ===== VALIDATE =====

int security_validate_reboot_token(long chat_id, int input_token) {

    time_t now = time(NULL);

    for (int i = 0; i <= g_token_ttl; i++) {

        int ts = (int)(now - i);

        int expected = (ts % 1000000) ^ make_token(chat_id, ts);

        if (expected == input_token) {

            log_msg(LOG_INFO,
                "Token accepted: chat_id=%ld token=%06d (age=%d sec)",
                chat_id, input_token, i);

            return 0;
        }
    }

    log_msg(LOG_WARN,
        "Invalid token: chat_id=%ld token=%06d",
        chat_id, input_token);

    return -1;
}
