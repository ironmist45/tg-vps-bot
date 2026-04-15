#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

typedef enum {
    LOG_ERROR = 0,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG
} log_level_t;

typedef struct {
    char token[128];
    long chat_id;
    int poll_timeout;
    char log_file[256];

    int token_ttl;

    log_level_t log_level;   // ✅ LOG_LEVEL
} config_t;

int config_load(const char *path, config_t *cfg);

#endif
