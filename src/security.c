#include "security.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SECRET_KEY 0x5F3759DF  // 🔐 можешь поменять
static int g_token_ttl = 60;

// простой hash (достаточно для этой задачи)
static int make_token(long chat_id, int ts) {
    return (int)((chat_id ^ ts ^ SECRET_KEY) % 1000000);
}

// ===== generate =====

int security_generate_reboot_token(long chat_id) {

    int ts = (int)time(NULL);

    int token = make_token(chat_id, ts);

    // кодируем timestamp в токен (чтобы не хранить)
    int final_token = (ts % 1000000) ^ token;

    log_msg(LOG_INFO,
        "Generated token: chat_id=%ld ts=%d token=%d",
        chat_id, ts, final_token);

    return final_token;
}

// ===== validate =====

int security_validate_reboot_token(long chat_id, int input_token) {

    time_t now = time(NULL);

    // проверяем окно времени (TTL)
    for (int i = 0; i <= g_token_ttl; i++) {

        int ts = (int)(now - i);

        int expected = (ts % 1000000) ^ make_token(chat_id, ts);

        if (expected == input_token) {
            log_msg(LOG_INFO,
                "Token accepted: chat_id=%ld token=%d",
                chat_id, input_token);
            return 0;
        }
    }

    log_msg(LOG_WARN,
        "Invalid token: chat_id=%ld token=%d",
        chat_id, input_token);

    return -1;
}
