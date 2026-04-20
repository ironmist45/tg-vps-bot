#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

typedef enum {
    LOG_ERROR = 0,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG
} log_level_t;

int logger_init(const char *path);
void logger_close();

void logger_set_level(log_level_t level);

void log_msg(log_level_t level, const char *fmt, ...);

// ✅ НОВОЕ
const char *logger_level_to_string(log_level_t level);

// ===== MODULE TAG MACROS =====

#define LOG_CFG(level, fmt, ...) \
    log_msg(level, "[CFG] " fmt, ##__VA_ARGS__)

#define LOG_NET(level, fmt, ...) \
    log_msg(level, "[NET] " fmt, ##__VA_ARGS__)

#define LOG_SEC(level, fmt, ...) \
    log_msg(level, "[SEC] " fmt, ##__VA_ARGS__)

#define LOG_SYS(level, fmt, ...) \
    log_msg(level, "[SYS] " fmt, ##__VA_ARGS__)

#define LOG_STATE(level, fmt, ...) \
    log_msg(level, "[STATE] " fmt, ##__VA_ARGS__)

#define LOG_CMD(level, fmt, ...) \
    log_msg(level, "[CMD] " fmt, ##__VA_ARGS__)

#define LOG_FALLBACK(level, fmt, ...) \
    fprintf(stderr, "[%s] [SYS] " fmt "\n", \
        logger_level_to_string(level), ##__VA_ARGS__)

#define LOG_EXEC(level, fmt, ...) \
    log_msg(level, "[EXEC] " fmt, ##__VA_ARGS__)

// 🔥 НОВЫЙ: для проверок (healthcheck / startup checks)
#define LOG_EXEC_CHECK(level, fmt, ...) \
    log_msg(level, "[EXECCHK] " fmt, ##__VA_ARGS__)

// 🔥 НОВЫЙ: CONTEXT LOGGING
#define LOG_CTX(LOG_MACRO, ctx, level, fmt, ...) \
    LOG_MACRO(level, fmt " (req=%04x chat_id=%ld user_id=%ld%s%s)", \
        ##__VA_ARGS__, \
        (ctx)->req_id, \
        (ctx)->chat_id, \
        (long)(ctx)->user_id, \
        ((ctx)->username) ? " user=@" : "", \
        ((ctx)->username) ? (ctx)->username : "")

#define LOG_NET_CTX(ctx, level, fmt, ...) \
    LOG_CTX(LOG_NET, ctx, level, fmt, ##__VA_ARGS__)

#define LOG_SEC_CTX(ctx, level, fmt, ...) \
    LOG_CTX(LOG_SEC, ctx, level, fmt, ##__VA_ARGS__)

#define LOG_STATE_CTX(ctx, level, fmt, ...) \
    LOG_CTX(LOG_STATE, ctx, level, fmt, ##__VA_ARGS__)

#define LOG_SYS_CTX(ctx, level, fmt, ...) \
    LOG_CTX(LOG_SYS, ctx, level, fmt, ##__VA_ARGS__)

#endif
