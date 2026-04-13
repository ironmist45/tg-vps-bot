#include "config.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define LINE_MAX_LEN 512

// ===== trim helpers =====

static char *ltrim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    return s;
}

static void rtrim(char *s) {
    char *back = s + strlen(s);
    while (back > s && isspace((unsigned char)*(--back))) {
        *back = '\0';
    }
}

static char *trim(char *s) {
    s = ltrim(s);
    rtrim(s);
    return s;
}

// ===== безопасное копирование =====

static int safe_copy(char *dst, size_t dst_size, const char *src) {
    if (strlen(src) >= dst_size) {
        return -1;
    }
    strcpy(dst, src);
    return 0;
}

// ===== парсинг числа =====

static int parse_long(const char *str, long *out) {
    char *end;
    long val = strtol(str, &end, 10);

    if (*end != '\0') {
        return -1;
    }

    *out = val;
    return 0;
}

static int parse_int(const char *str, int *out) {
    char *end;
    long val = strtol(str, &end, 10);

    if (*end != '\0' || val <= 0) {
        return -1;
    }

    *out = (int)val;
    return 0;
}

// ===== основной загрузчик =====

int config_load(const char *path, config_t *cfg) {
    FILE *f = fopen(path, "r");
    if (!f) {
        log_msg(LOG_ERROR, "Cannot open config file: %s", path);
        return -1;
    }

    // значения по умолчанию
    memset(cfg, 0, sizeof(config_t));
    cfg->poll_timeout = 30;
    strcpy(cfg->log_file, "/var/log/tg-bot.log");

    char line[LINE_MAX_LEN];
    int line_num = 0;

    while (fgets(line, sizeof(line), f)) {
        line_num++;

        // убираем \n
        line[strcspn(line, "\n")] = 0;

        char *str = trim(line);

        // пропуск пустых строк и комментариев
        if (str[0] == '\0' || str[0] == '#') {
            continue;
        }

        // ищем '='
        char *eq = strchr(str, '=');
        if (!eq) {
            log_msg(LOG_WARN, "Invalid line %d (no '=')", line_num);
            continue;
        }

        *eq = '\0';
        char *key = trim(str);
        char *value = trim(eq + 1);

        // ===== TOKEN =====
        if (strcmp(key, "TOKEN") == 0) {
            if (safe_copy(cfg->token, sizeof(cfg->token), value) != 0) {
                log_msg(LOG_ERROR, "TOKEN too long");
                fclose(f);
                return -1;
            }
        }

        // ===== CHAT_ID =====
        else if (strcmp(key, "CHAT_ID") == 0) {
            if (parse_long(value, &cfg->chat_id) != 0) {
                log_msg(LOG_ERROR, "Invalid CHAT_ID at line %d", line_num);
                fclose(f);
                return -1;
            }
        }

        // ===== POLL_TIMEOUT =====
        else if (strcmp(key, "POLL_TIMEOUT") == 0) {
            if (parse_int(value, &cfg->poll_timeout) != 0) {
                log_msg(LOG_WARN, "Invalid POLL_TIMEOUT, using default");
                cfg->poll_timeout = 30;
            }
        }

        // ===== LOG_FILE =====
        else if (strcmp(key, "LOG_FILE") == 0) {
            if (safe_copy(cfg->log_file, sizeof(cfg->log_file), value) != 0) {
                log_msg(LOG_WARN, "LOG_FILE too long, using default");
                strcpy(cfg->log_file, "/var/log/tg-bot.log");
            }
        }

        else {
            log_msg(LOG_DEBUG, "Unknown config key: %s", key);
        }
    }

    fclose(f);

    // ===== финальная валидация =====

    if (cfg->token[0] == '\0') {
        log_msg(LOG_ERROR, "TOKEN is missing in config");
        return -1;
    }

    if (cfg->chat_id == 0) {
        log_msg(LOG_ERROR, "CHAT_ID is missing or invalid");
        return -1;
    }

    log_msg(LOG_INFO, "Config loaded successfully");
    log_msg(LOG_DEBUG, "CHAT_ID=%ld", cfg->chat_id);
    log_msg(LOG_DEBUG, "POLL_TIMEOUT=%d", cfg->poll_timeout);
    log_msg(LOG_DEBUG, "LOG_FILE=%s", cfg->log_file);

    return 0;
}
