#include "security.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_TOKENS 16
#define RATE_LIMIT_SEC 10

typedef struct {
    long chat_id;
    int token;
    time_t created_at;
    int used;
} reboot_token_t;

static reboot_token_t tokens[MAX_TOKENS];

// config (runtime)
static long g_allowed_chat_id = 0;
static int g_token_ttl = 60;

// rate limit (лучше per-chat, но ок)
static time_t last_request_time = 0;

// ===== init =====

void security_set_allowed_chat(long chat_id) {
    g_allowed_chat_id = chat_id;
}

void security_set_token_ttl(int ttl) {
    if (ttl > 0 && ttl <= 3600)
        g_token_ttl = ttl;
}

// ===== access =====

int security_is_allowed_chat(long chat_id) {
    return chat_id == g_allowed_chat_id;
}

int security_validate_text(const char *text) {
    if (!text) return -1;
    if (strlen(text) > 1024) return -1;
    return 0;
}

// ===== cleanup =====

static void cleanup_tokens() {
    time_t now = time(NULL);

    for (int i = 0; i < MAX_TOKENS; i++) {

        if (tokens[i].chat_id == 0)
            continue;

        // удалить использованные или просроченные
        if (tokens[i].used ||
            (now - tokens[i].created_at > g_token_ttl)) {

            memset(&tokens[i], 0, sizeof(tokens[i]));
        }
    }
}

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

    // сначала чистим мусор
    cleanup_tokens();

    for (int i = 0; i < MAX_TOKENS; i++) {
        if (tokens[i].chat_id == 0) {
            return &tokens[i];
        }
    }

    // fallback — перезаписать самый старый
    int oldest = 0;
    for (int i = 1; i < MAX_TOKENS; i++) {
        if (tokens[i].created_at < tokens[oldest].created_at) {
            oldest = i;
        }
    }

    log_msg(LOG_WARN, "Token table full, reusing oldest slot");

    memset(&tokens[oldest], 0, sizeof(tokens[oldest]));
    return &tokens[oldest];
}

// ===== generate =====

int security_generate_reboot_token(long chat_id) {

    time_t now = time(NULL);

    // rate limit
    if (now - last_request_time < RATE_LIMIT_SEC) {
        log_msg(LOG_WARN, "Rate limit hit for chat_id=%ld", chat_id);
        return -1;
    }

    reboot_token_t *t = alloc_token();
    if (!t) {
        log_msg(LOG_ERROR, "No free token slots");
        return -1;
    }

    // защита от случайных совпадений
    int token;
    do {
        token = rand() % 900000 + 100000;
    } while (find_token(chat_id, token) != NULL);

    t->chat_id = chat_id;
    t->token = token;
    t->created_at = now;
    t->used = 0;

    last_request_time = now;

    log_msg(LOG_INFO,
            "Reboot token generated: chat_id=%ld token=%d",
            chat_id, token);

    return token;
}

// ===== validate =====

int security_validate_reboot_token(long chat_id, int token) {

    cleanup_tokens();

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

    if (now - t->created_at > g_token_ttl) {
        log_msg(LOG_WARN,
                "Token expired: chat_id=%ld token=%d",
                chat_id, token);

        memset(t, 0, sizeof(*t));
        return -1;
    }

    t->used = 1;

    log_msg(LOG_INFO,
            "Token accepted: chat_id=%ld token=%d",
            chat_id, token);

    return 0;
}
