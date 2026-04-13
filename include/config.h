#ifndef CONFIG_H
#define CONFIG_H

#define MAX_TOKEN_LEN 256
#define MAX_PATH_LEN 256

typedef struct {
    char token[MAX_TOKEN_LEN];
    long chat_id;
    int poll_timeout;
    char log_file[MAX_PATH_LEN];
} config_t;

int config_load(const char *path, config_t *cfg);

#endif
