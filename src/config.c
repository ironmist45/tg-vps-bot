#include "config.h"
#include "logger.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>  // для strcasecmp

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
    cfg->token_ttl = 60;
    safe_copy(cfg->log_file, sizeof(cfg->log_file), "/var/log/tg-bot.log");
    cfg->log_level = LOG_INFO;  // ✅ default logging level

    char line[LINE_MAX_LEN];
    int line_num = 0;

    while (fgets(line, sizeof(line), f)) {
        line_num++;

        // убрать \n
        line[strcspn(line, "\n")] = 0;

        char *str = trim(line);

        // пропуск пустых строк и комментариев
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
        // 🔥 убрать \r (Windows строки)
        key[strcspn(key, "\r")] = 0;
        value[strcspn(value, "\r")] = 0;

        // ===== case-insensitive parsing =====
        log_msg(LOG_ERROR, "KEY RAW = '%s'", key);

        if (strcasecmp(key, "TOKEN") == 0) {
            if (safe_copy(cfg->token, sizeof(cfg->token), value) != 0) {
                log_msg(LOG_ERROR, "TOKEN too long");
                fclose(f);
                return -1;
            }
        }
        else if (strcasecmp(key, "CHAT_ID") == 0) {
            if (parse_long(value, &cfg->chat_id) != 0) {
                log_msg(LOG_ERROR, "Invalid CHAT_ID at line %d", line_num);
                fclose(f);
                return -1;
            }
        }
        else if (strcasecmp(key, "POLL_TIMEOUT") == 0) {
            if (parse_int(value, &cfg->poll_timeout) != 0) {
                log_msg(LOG_WARN, "Invalid POLL_TIMEOUT, using default");
                cfg->poll_timeout = 30;
            }
        }
        else if (strcasecmp(key, "LOG_FILE") == 0) {
            if (safe_copy(cfg->log_file, sizeof(cfg->log_file), value) != 0) {
                log_msg(LOG_WARN, "LOG_FILE too long, using default");
                safe_copy(cfg->log_file, sizeof(cfg->log_file), "/var/log/tg-bot.log");
            }
        }
        else if (strcasecmp(key, "TOKEN_TTL") == 0) {
            if (parse_int(value, &cfg->token_ttl) != 0) {
                log_msg(LOG_WARN, "Invalid TOKEN_TTL, using default");
                cfg->token_ttl = 60;
            }
        }
else if (strcasecmp(key, "LOG_LEVEL") == 0) {
    log_msg(LOG_ERROR, "LOG_LEVEL RAW VALUE = '%s'", value);
    if (strcasecmp(value, "ERROR") == 0) {
        cfg->log_level = LOG_ERROR;
    } else if (strcasecmp(value, "WARN") == 0) {
        cfg->log_level = LOG_WARN;
    } else if (strcasecmp(value, "INFO") == 0) {
        cfg->log_level = LOG_INFO;
    } else if (strcasecmp(value, "DEBUG") == 0) {
        cfg->log_level = LOG_DEBUG;
    } else {
        log_msg(LOG_WARN, "Invalid LOG_LEVEL '%s', using default", value);
        cfg->log_level = LOG_INFO;
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

    // ===== минимальный лог =====
    log_msg(LOG_INFO, "Config loaded (log_level=%d)", cfg->log_level);

    return 0;
}
