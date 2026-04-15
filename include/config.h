#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>
#include "logger.h"   // ✅ используем единый enum

typedef struct {
    char token[128];
    long chat_id;
    int poll_timeout;
    char log_file[256];

    int token_ttl;

    log_level_t log_level;   // теперь из logger.h
} config_t;

int config_load(const char *path, config_t *cfg);

#endif
