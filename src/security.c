#include "security.h"
#include "logger.h"

#include <stdlib.h>
#include <time.h>
#include <string.h>

#define MAX_TOKENS 16
#define TOKEN_TTL 60        // ⏱ 60 секунд
#define RATE_LIMIT_SEC 10   // ⏱ защита от спама

typedef struct {
    long chat_id;
    int token;
    time_t created_at;
    int used;
} reboot_token_t;

static reboot_token_t tokens[MAX_TOKENS];
static time_t last_request_time[MAX_TOKENS];

// ===== utils =====

static reboot_token_t *find_token(long chat_id, int token) {
    for (int i = 0; i < MAX_TOKENS; i++) {
        if (tokens[i].chat_id == chat_id &&
            tokens[i].token == token) {
            return &tokens[i];
        }
    }
    return NULL;
}

static reboot_token_t *alloc_token() {
    for (int i = 0; i < MAX_TOKENS; i++) {
        if (tokens[i].chat_id == 0 || tokens[i].used) {
            return &tokens[i];
        }
    }
    return NULL;
}

// ===== API =====

// генерация токена
int security_generate_reboot_token(long chat_id) {

    time_t now = time(NULL);

    // ⏱ rate limit
    for (int i = 0; i < MAX_TOKENS; i++) {
        if (last_request_time[i] != 0 &&
            now - last_request_time[i] < RATE_LIMIT_SEC) {
            log_msg(LOG_WARN, "Rate limit hit for chat_id=%ld", chat_id);
            return -1;
        }
    }

    reboot_token_t *t = alloc_token();
    if (!t)
        return -1;

    t->chat_id = chat_id;
    t->token = rand() % 900000 + 100000; // 6-digit
    t->created_at = now;
    t->used = 0;

    // запоминаем время
    for (int i = 0; i < MAX_TOKENS; i++) {
        if (last_request_time[i] == 0) {
            last_request_time[i] = now;
            break;
        }
    }

    log_msg(LOG_INFO,
            "Reboot token generated: chat_id=%ld token=%d",
            chat_id, t->token);

    return t->token;
}

// проверка токена
int security_validate_reboot_token(long chat_id, int token) {

    reboot_token_t *t = find_token(chat_id, token);

    if (!t) {
        log_msg(LOG_WARN,
                "Invalid token: chat_id=%ld token=%d",
                chat_id, token);
        return -1;
    }

    if (t->used) {
        log_msg(LOG_WARN,
                "Token already used: chat_id=%ld token=%d",
                chat_id, token);
        return -1;
    }

    time_t now = time(NULL);

    // ⏱ TTL check
    if (now - t->created_at > TOKEN_TTL) {
        log_msg(LOG_WARN,
                "Token expired: chat_id=%ld token=%d",
                chat_id, token);
        return -1;
    }

    // ✅ инвалидируем
    t->used = 1;

    log_msg(LOG_INFO,
            "Token accepted: chat_id=%ld token=%d",
            chat_id, token);

    return 0;
}
