#include "security.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

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
static unsigned int g_runtime_salt = 0;
static int g_last_token = -1;
static time_t g_last_token_time = 0;
static int g_failed_attempts = 0;
static time_t g_block_until = 0;
// 🆕 rate limit
static time_t g_last_cmd_time = 0;
static int g_cmd_burst = 0;

// ===== INIT =====

void security_init(void) {
   unsigned int seed = (unsigned int)(
        time(NULL) ^ getpid() ^ clock()
);

    srand(seed);
    g_runtime_salt = (unsigned int)rand();

    LOG_SEC(LOG_INFO, "Security initialized (stateless tokens)");
    LOG_SEC(LOG_DEBUG, "runtime salt=0x%X", g_runtime_salt);
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

    // 🆕 защита от неинициализированного состояния!
    if (g_allowed_chat_id == 0) {
        LOG_SEC(LOG_ERROR, "allowed_chat_id not initialized");
        return 0;
    }

    return chat_id == g_allowed_chat_id;
}

int security_validate_text(const char *text) {
    
    if (!text)
        return -1;

    size_t len = strlen(text);

    if (len == 0 || len > 1024)
        return -1;

    // запрещаем бинарные / управляющие символы
    for (size_t i = 0; i < len; i++) {
        
        unsigned char c = text[i];

        if (c < 32 &&
            c != '\n' &&
            c != '\r' &&
            c != '\t') {
            return -1;
        }
    }

    return 0;

}

int security_check_access(long chat_id, const char *cmd) {

    int allowed = security_is_allowed_chat(chat_id); // использовать { return 0;} - 🚫 для теста запретить доступ всем!

    LOG_SEC(LOG_INFO,
      "ACCESS CHECK: chat_id=%ld cmd=%s result=%s",
       chat_id,
       cmd ? cmd : "NULL",
       allowed ? "ALLOW" : "DENY"
    );

    if (!allowed) {
        LOG_STATE(LOG_WARN,
            "ACCESS DENIED: cmd=%s (chat_id=%ld)",
            cmd ? cmd : "NULL", chat_id);

        return -1;
    }

    return 0;
}

// ===== Bruteforce helper =====

static void register_failed_attempt(time_t now) {

    g_failed_attempts++;

    LOG_SEC(LOG_DEBUG,
        "failed attempts: %d",
        g_failed_attempts);

    if (g_failed_attempts >= 5) {

        g_block_until = now + 30;
        g_failed_attempts = 0;

        LOG_SEC(LOG_WARN,
            "Too many invalid tokens, blocking for 30 seconds");
    }
}

// ===== RATE LIMIT =====

int security_rate_limit(void) {

    time_t now = time(NULL);

    if (now == g_last_cmd_time) {
        g_cmd_burst++;

        if (g_cmd_burst > 5) {
            LOG_SEC(LOG_WARN, "Rate limit triggered");
            return -1;
        }
    } else {
        g_last_cmd_time = now;
        g_cmd_burst = 1;
    }

    return 0;
}

// ===== TOKEN CORE =====

// 🔥 улучшенный hash (без отрицательных значений)
static int make_token(long chat_id, time_t ts) {

    unsigned int x = (unsigned int)(chat_id ^ ts ^ TOKEN_SALT ^ g_runtime_salt);

    // немного "перемешаем" биты
    x ^= (x >> 16);
    x *= 0x45d9f3b;
    x ^= (x >> 16);

    return (int)(x % 1000000);
}

// ===== GENERATE =====

int security_generate_reboot_token(long chat_id) {

    time_t ts = time(NULL);

    int token = make_token(chat_id, ts);

    int final_token = (ts % 1000000) ^ token;

    LOG_SEC(LOG_INFO,
        "Generated token: chat_id=%ld ts=%d token=%06d",
        chat_id, ts, final_token);
    LOG_SEC(LOG_DEBUG,
        "token components: ts_mod=%d raw=%d",
        ts % 1000000, token);

    g_last_token = final_token;
    g_last_token_time = ts;

    return final_token;
    
}

// ===== VALIDATE =====

int security_validate_reboot_token(long chat_id, int input_token) {

    // 🔥 проверка формата токена
    if (input_token < 0 || input_token > 999999) {
        LOG_SEC(LOG_WARN,
            "Invalid token format: %d (chat_id=%ld)",
            input_token, chat_id);
        return -1;
    }

    time_t now = time(NULL);

    if (now < g_block_until) {
        LOG_SEC(LOG_WARN, "Token validation blocked (bruteforce)");
        return -1;
    }

    for (int i = 0; i <= g_token_ttl; i++) {

        time_t ts = (now - i);

        int expected = (ts % 1000000) ^ make_token(chat_id, ts);

        LOG_SEC(LOG_DEBUG,
            "check: ts=%d expected=%06d input=%06d",
            ts, expected, input_token);

        if (expected == input_token) {

            // проверяем что это именно последний токен
            if (input_token == g_last_token &&
                (now - g_last_token_time) <= g_token_ttl) {

                // инвалидируем после использования
                g_last_token = -1;

                LOG_SEC(LOG_INFO,
                    "Token accepted: chat_id=%ld token=%06d (age=%d sec)",
                    chat_id, input_token, i);

                // 🔥 reset bruteforce state
                g_failed_attempts = 0;
                g_block_until = 0;
                
                return 0;
            }

            LOG_SEC(LOG_WARN,
                "Replay or expired token: %06d",
                input_token);

            register_failed_attempt(now);

            return -1;
        }
    }

    LOG_SEC(LOG_WARN,
        "Invalid token: chat_id=%ld token=%06d",
        chat_id, input_token);

    register_failed_attempt(now);
    
return -1;
    
}
