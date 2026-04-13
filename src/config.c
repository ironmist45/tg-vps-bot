#include "config.h"
#include "logger.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>

#define LINE_MAX_LEN 512

int config_load(const char *path, config_t *cfg) {
    FILE *f = fopen(path, "r");
    if (!f) {
        log_msg(LOG_ERROR, "Cannot open config file: %s", path);
        return -1;
    }

    // ===== defaults =====
    memset(cfg, 0, sizeof(config_t));
    cfg->poll_timeout = 30;
    cfg->token_ttl = 60;   // ✅ ДОБАВИЛИ
    safe_copy(cfg->log_file, sizeof(cfg->log_file), "/var/log/tg-bot.log");

    char line[LINE_MAX_LEN];
    int line_num = 0;

    while (fgets(line, sizeof(line), f)) {
        line_num++;

        line[strcspn(line, "\n")] = 0;

        char *str = trim(line);

        if (str[0] == '\0' || str[0] == '#')
            continue;

        char *eq = strchr(str, '=');
        if (!eq) {
            log_msg(LOG_WARN, "Invalid line %d (no '=')", line_num);
            continue;
        }

        *eq = '\0';

        char *key = trim(str);
        char *value = trim(eq + 1);

        if (strcmp(key, "TOKEN") == 0) {
            if (safe_copy(cfg->token, sizeof(cfg->token), value) != 0) {
                log_msg(LOG_ERROR, "TOKEN too long");
                fclose(f);
                return -1;
            }
        }
        else if (strcmp(key, "CHAT_ID") == 0) {
            if (parse_long(value, &cfg->chat_id) != 0) {
                log_msg(LOG_ERROR, "Invalid CHAT_ID at line %d", line_num);
                fclose(f);
                return -1;
            }
        }
        else if (strcmp(key, "POLL_TIMEOUT") == 0) {
            if (parse_int(value, &cfg->poll_timeout) != 0) {
                log_msg(LOG_WARN, "Invalid POLL_TIMEOUT, using default");
                cfg->poll_timeout = 30;
            }
        }
        else if (strcmp(key, "LOG_FILE") == 0) {
            if (safe_copy(cfg->log_file, sizeof(cfg->log_file), value) != 0) {
                log_msg(LOG_WARN, "LOG_FILE too long, using default");
                safe_copy(cfg->log_file, sizeof(cfg->log_file), "/var/log/tg-bot.log");
            }
        }
        else if (strcmp(key, "TOKEN_TTL") == 0) {   // ✅ ДОБАВИЛИ
            if (parse_int(value, &cfg->token_ttl) != 0) {
                log_msg(LOG_WARN, "Invalid TOKEN_TTL, using default");
                cfg->token_ttl = 60;
            }
        }
        else {
            log_msg(LOG_DEBUG, "Unknown config key: %s", key);
        }
    }

    fclose(f);

    // ===== validation =====

    if (cfg->token[0] == '\0') {
        log_msg(LOG_ERROR, "TOKEN is missing");
        return -1;
    }

    if (cfg->chat_id == 0) {
        log_msg(LOG_ERROR, "CHAT_ID is missing or invalid");
        return -1;
    }

    log_msg(LOG_INFO, "Config loaded");
    log_msg(LOG_INFO, "TOKEN_TTL: %d sec", cfg->token_ttl);  // ✅ ДОБАВИЛИ

    return 0;
}
