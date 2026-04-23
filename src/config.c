/**
 * tg-bot - Telegram bot for system administration
 * 
 * config.c - Configuration file parsing and validation
 * 
 * Loads and parses the bot configuration from a key=value file.
 * Supports case-insensitive keys and provides sensible defaults
 * for optional parameters.
 * 
 * Supported configuration keys:
 *   TOKEN        - Telegram Bot API token (required)
 *   CHAT_ID      - Allowed Telegram chat ID (required)
 *   POLL_TIMEOUT - Long polling timeout in seconds (default: 30)
 *   LOG_FILE     - Path to log file (default: /var/log/tg-bot.log)
 *   TOKEN_TTL    - Reboot token time-to-live in seconds (default: 60)
 *   LOG_LEVEL    - Logging level: ERROR, WARN, INFO, DEBUG (default: INFO)
 * 
 * MIT License
 * 
 * Copyright (c) 2026
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "config.h"
#include "logger.h"
#include "security.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>  // for strcasecmp

// Maximum length of a single configuration line
#define LINE_MAX_LEN 512

// ============================================================================
// CONFIGURATION LOADING
// ============================================================================

/**
 * Load and parse configuration file
 * 
 * @param path  Path to configuration file
 * @param cfg   Pointer to config_t structure to populate
 * @return      0 on success, -1 on error
 */
int config_load(const char *path, config_t *cfg) {
    // ------------------------------------------------------------------------
    // Open configuration file
    // ------------------------------------------------------------------------
    FILE *f = fopen(path, "r");
    if (!f) {
        LOG_CFG(LOG_ERROR, "Cannot open config file: %s", path);
        return -1;
    }

    // ------------------------------------------------------------------------
    // Set default values
    // ------------------------------------------------------------------------
    memset(cfg, 0, sizeof(config_t));
    cfg->poll_timeout = 30;
    cfg->token_ttl = 60;
    safe_copy(cfg->log_file, sizeof(cfg->log_file), "/var/log/tg-bot.log");
    cfg->log_level = LOG_INFO;

    char line[LINE_MAX_LEN];
    int line_num = 0;

    // ------------------------------------------------------------------------
    // Parse file line by line
    // ------------------------------------------------------------------------
    while (fgets(line, sizeof(line), f)) {
        line_num++;

        // Remove trailing newline character
        line[strcspn(line, "\n")] = 0;

        char *str = trim(line);

        // Skip empty lines and comments
        if (str[0] == '\0' || str[0] == '#')
            continue;

        // Find '=' separator
        char *eq = strchr(str, '=');
        if (!eq) {
            LOG_CFG(LOG_WARN, "Invalid line %d (no '=')", line_num);
            continue;
        }

        // Split into key and value
        *eq = '\0';

        char *key = trim(str);
        char *value = trim(eq + 1);
        
        // Remove carriage return (Windows line endings)
        key[strcspn(key, "\r")] = 0;
        value[strcspn(value, "\r")] = 0;
        
        LOG_CFG(LOG_DEBUG, "line=%d key='%s' value='%s'", line_num, key, value);

        // --------------------------------------------------------------------
        // Parse known configuration keys (case-insensitive)
        // --------------------------------------------------------------------
        
        if (strcasecmp(key, "TOKEN") == 0) {
            // Required: Telegram Bot API token
            if (safe_copy(cfg->token, sizeof(cfg->token), value) != 0) {
                LOG_CFG(LOG_ERROR, "TOKEN too long");
                fclose(f);
                return -1;
            }
        }
        else if (strcasecmp(key, "CHAT_ID") == 0) {
            // Required: Allowed Telegram chat ID
            if (parse_long(value, &cfg->chat_id) != 0) {
                LOG_CFG(LOG_ERROR, "Invalid CHAT_ID at line %d", line_num);
                fclose(f);
                return -1;
            }
        }
        else if (strcasecmp(key, "POLL_TIMEOUT") == 0) {
            // Optional: Long polling timeout (seconds)
            if (parse_int(value, &cfg->poll_timeout) != 0) {
                LOG_CFG(LOG_WARN, "Invalid POLL_TIMEOUT, using default");
                cfg->poll_timeout = 30;
            }
        }
        else if (strcasecmp(key, "LOG_FILE") == 0) {
            // Optional: Path to log file
            if (safe_copy(cfg->log_file, sizeof(cfg->log_file), value) != 0) {
                LOG_CFG(LOG_WARN, "LOG_FILE too long, using default");
                safe_copy(cfg->log_file, sizeof(cfg->log_file), "/var/log/tg-bot.log");
            }
        }
        else if (strcasecmp(key, "TOKEN_TTL") == 0) {
            // Optional: Reboot token time-to-live (seconds)
            if (parse_int(value, &cfg->token_ttl) != 0) {
                LOG_CFG(LOG_WARN, "Invalid TOKEN_TTL, using default");
                cfg->token_ttl = 60;
            }
        }
        else if (strcasecmp(key, "LOG_LEVEL") == 0) {
            // Optional: Logging verbosity level
            if (strcasecmp(value, "ERROR") == 0) {
                cfg->log_level = LOG_ERROR;
            } else if (strcasecmp(value, "WARN") == 0) {
                cfg->log_level = LOG_WARN;
            } else if (strcasecmp(value, "INFO") == 0) {
                cfg->log_level = LOG_INFO;
            } else if (strcasecmp(value, "DEBUG") == 0) {
                cfg->log_level = LOG_DEBUG;
            } else {
                LOG_CFG(LOG_WARN, "Invalid LOG_LEVEL '%s', using default", value);
                cfg->log_level = LOG_INFO;
            }
        }
        else {
            // Unknown key (non-fatal, just log for debugging)
            LOG_CFG(LOG_DEBUG, "Unknown config key: %s", key);
        }
    }
    
    fclose(f);

    // ------------------------------------------------------------------------
    // Validate required fields
    // ------------------------------------------------------------------------
    if (cfg->token[0] == '\0') {
        LOG_CFG(LOG_ERROR, "TOKEN is missing");
        return -1;
    }

    if (cfg->chat_id == 0) {
        LOG_CFG(LOG_ERROR, "CHAT_ID is missing or invalid");
        return -1;
    }

    // ------------------------------------------------------------------------
    // Configuration loaded successfully
    // ------------------------------------------------------------------------
    LOG_CFG(LOG_INFO, "Config loaded (log_level=%d)", cfg->log_level);
    LOG_CFG(LOG_DEBUG, "parsed log_level=%s",
            logger_level_to_string(cfg->log_level));
    
    return 0;
}

int config_reload(const char *path, config_t *cfg) {
    LOG_STATE(LOG_INFO, "Reload config");
    
    config_t new_cfg;
    if (config_load(path, &new_cfg) != 0) {
        return -1;
    }
    
    if (strcmp(cfg->log_file, new_cfg.log_file) != 0) {
        logger_reopen(new_cfg.log_file);
    }
    
    logger_set_level(new_cfg.log_level);
    security_set_allowed_chat(new_cfg.chat_id);
    security_set_token_ttl(new_cfg.token_ttl);
    
    *cfg = new_cfg;
    config_log(cfg);
    
    return 0;
}

// ============================================================================
// UTILITIES
// ============================================================================

void config_log(const config_t *cfg) {
    LOG_CFG(LOG_INFO, "===== CONFIG =====");
    LOG_CFG(LOG_INFO, "CHAT_ID: %ld", cfg->chat_id);
    LOG_CFG(LOG_INFO, "TOKEN_TTL: %d", cfg->token_ttl);
    LOG_CFG(LOG_INFO, "POLL_TIMEOUT: %d", cfg->poll_timeout);
    LOG_CFG(LOG_INFO, "LOG_FILE: %s", cfg->log_file);
    LOG_CFG(LOG_INFO, "LOG_LEVEL: %s", logger_level_to_string(cfg->log_level));
}
