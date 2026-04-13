#include "security.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h> // 🔥 FIX

#define SECRET_KEY 0x5F3759DF

static long g_allowed_chat_id = 0;
static int g_token_ttl = 60;

// ===== INIT =====

void security_init(void) {
    srand(time(NULL));
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
    if (strlen(text) > 1024) return -1;
    return 0;
}

// ===== TOKEN CORE =====

// простой hash
static int make_token(long chat_id, int ts) {
    return (int)((chat_id ^ ts ^ SECRET_KEY) % 1000000);
}

// ===== GENERATE =====

int security_generate_reboot_token(long chat_id) {

    int ts = (int)time(NULL);

    int token = make_token(chat_id, ts);

    int final_token = (ts % 1000000) ^ token;

    log_msg(LOG_INFO,
        "Generated token: chat_id=%ld ts=%d token=%d",
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
