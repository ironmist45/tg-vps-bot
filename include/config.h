#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

typedef struct {
    char token[128];
    long chat_id;
    int poll_timeout;
    char log_file[256];

    int token_ttl;   // ✅ ДОБАВИЛИ
} config_t;

int config_load(const char *path, config_t *cfg);

#endif
