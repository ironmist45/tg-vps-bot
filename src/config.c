/* tg-bot — config.c — Configuration file parsing and validation. MIT License © 2026 ironmist45 */

/*
 * Loads and parses the bot configuration from a key=value file.
 * Supports case-insensitive keys and provides sensible defaults
 * for optional parameters.
 *
 * Supported configuration keys:
 *   TOKEN          - Telegram Bot API token (required)
 *   CHAT_ID        - Allowed Telegram chat ID (required)
 *   LOG_FILE       - Path to log file (default: /var/log/tg-bot.log)
 *   TOKEN_TTL      - Confirmation token TTL in seconds (default: 60)
 *   LOG_LEVEL      - Logging level: ERROR, WARN, INFO, DEBUG (default: INFO)
 *   TOTP_SECRET    - Base32 TOTP secret for 2FA (optional, default: disabled)
 *   UPLOAD_ENABLED - Enable file upload: yes/true/1/enabled (default: disabled)
 *   UPLOAD_DIR     - Directory for uploaded files (default: /var/www/html/uploads)
 *   SSH_KEYS_PATH  - Path to authorized_keys file (optional, default: disabled)
 */

#include "config.h"
#include "logger.h"
#include "security.h"
#include "totp.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>  /* strcasecmp */
#include <sys/mman.h> /* mlock()    */

/* Maximum length of a single configuration line */
#define CONFIG_LINE_MAX 512

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
    FILE *f = fopen(path, "r");
    if (!f) {
        LOG_CFG(LOG_ERROR, "Cannot open config file: %s", path);
        return -1;
    }

    /* Set default values */
    memset(cfg, 0, sizeof(config_t));
    cfg->token_ttl = 60;
    safe_copy(cfg->log_file,         sizeof(cfg->log_file),         "/var/log/tg-bot.log");
    cfg->log_level = LOG_INFO;

    /* Default system paths */
    safe_copy(cfg->sudo_path,        sizeof(cfg->sudo_path),        "/usr/bin/sudo");
    safe_copy(cfg->systemctl_path,   sizeof(cfg->systemctl_path),   "/bin/systemctl");
    safe_copy(cfg->journalctl_path,  sizeof(cfg->journalctl_path),  "/bin/journalctl");
    safe_copy(cfg->f2b_wrapper_path, sizeof(cfg->f2b_wrapper_path), "/usr/local/bin/f2b-wrapper");

    /* Upload disabled by default — must be explicitly enabled in config */
    cfg->upload_enabled = 0;
    safe_copy(cfg->upload_dir, sizeof(cfg->upload_dir), "/var/www/html/uploads");

    /* TOTP disabled by default */
    cfg->totp_secret[0]     = '\0';
    cfg->totp_setup_enabled = 0;

    /* SSH keys — disabled by default, must be explicitly configured */
    cfg->ssh_keys_path[0] = '\0';

    char line[CONFIG_LINE_MAX];
    int  line_num = 0;

    while (fgets(line, sizeof(line), f)) {
        line_num++;

        line[strcspn(line, "\n")] = '\0';

        char *str = trim(line);

        if (str[0] == '\0' || str[0] == '#')
            continue;

        char *eq = strchr(str, '=');
        if (!eq) {
            LOG_CFG(LOG_WARN, "Invalid line %d (no '=')", line_num);
            continue;
        }

        *eq = '\0';

        char *key   = trim(str);
        char *value = trim(eq + 1);

        key[strcspn(key, "\r")]     = '\0';
        value[strcspn(value, "\r")] = '\0';

        /*
         * Mask sensitive values in debug output — TOKEN and TOTP_SECRET
         * must never appear in log files even at DEBUG level.
         */
        int is_sensitive = (strcasecmp(key, "TOKEN")       == 0 ||
                            strcasecmp(key, "TOTP_SECRET") == 0);
        LOG_CFG(LOG_DEBUG, "line=%d key='%s' value='%s'",
                line_num, key, is_sensitive ? "***" : value);

        if (strcasecmp(key, "TOKEN") == 0) {
            if (safe_copy(cfg->token, sizeof(cfg->token), value) != 0) {
                LOG_CFG(LOG_ERROR, "TOKEN too long");
                fclose(f);
                return -1;
            }
        }
        else if (strcasecmp(key, "CHAT_ID") == 0) {
            if (parse_long(value, &cfg->chat_id) != 0) {
                LOG_CFG(LOG_ERROR, "Invalid CHAT_ID at line %d", line_num);
                fclose(f);
                return -1;
            }
        }
        else if (strcasecmp(key, "LOG_FILE") == 0) {
            if (safe_copy(cfg->log_file, sizeof(cfg->log_file), value) != 0) {
                LOG_CFG(LOG_WARN, "LOG_FILE too long, using default");
                safe_copy(cfg->log_file, sizeof(cfg->log_file), "/var/log/tg-bot.log");
            }
        }
        else if (strcasecmp(key, "TOKEN_TTL") == 0) {
            if (parse_int(value, &cfg->token_ttl) != 0) {
                LOG_CFG(LOG_WARN, "Invalid TOKEN_TTL, using default");
                cfg->token_ttl = 60;
            }
        }
        else if (strcasecmp(key, "LOG_LEVEL") == 0) {
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
        else if (strcasecmp(key, "TOTP_SECRET") == 0) {
            /*
             * Validate before storing — catch typos in config early
             * rather than getting a cryptic error at confirmation time.
             */
            if (value[0] == '\0') {
                cfg->totp_secret[0] = '\0';
                LOG_CFG(LOG_DEBUG, "TOTP_SECRET is empty — TOTP 2FA disabled");
            } else if (totp_validate_secret(value) != 0) {
                LOG_CFG(LOG_WARN,
                    "TOTP_SECRET at line %d is invalid (bad base32) — TOTP 2FA disabled",
                    line_num);
                cfg->totp_secret[0] = '\0';
            } else {
                if (safe_copy(cfg->totp_secret, sizeof(cfg->totp_secret), value) != 0) {
                    LOG_CFG(LOG_WARN, "TOTP_SECRET too long — TOTP 2FA disabled");
                    cfg->totp_secret[0] = '\0';
                } else {
                    /*
                     * Lock the page containing the TOTP secret to prevent it
                     * from being swapped to disk — swap file is readable by root
                     * and the secret must not appear there in plaintext.
                     */
                    if (mlock(cfg->totp_secret, sizeof(cfg->totp_secret)) != 0)
                        LOG_CFG(LOG_WARN, "mlock(totp_secret) failed — secret may be swapped");
                    LOG_CFG(LOG_INFO, "TOTP_SECRET loaded (TOTP 2FA enabled)");
                }
            }
        }
        else if (strcasecmp(key, "TOTP_SETUP") == 0) {
            if (strcasecmp(value, "enabled") == 0) {
                cfg->totp_setup_enabled = 1;
                LOG_CFG(LOG_INFO, "TOTP_SETUP: enabled");
            } else {
                cfg->totp_setup_enabled = 0;
                LOG_CFG(LOG_DEBUG, "TOTP_SETUP: disabled");
            }
        }
        else if (strcasecmp(key, "UPLOAD_ENABLED") == 0) {
            /*
             * Accept yes/true/1/enabled as truthy — anything else is false.
             * Consistent with TOTP_SETUP=enabled/disabled pattern.
             */
            cfg->upload_enabled = (strcasecmp(value, "yes")     == 0 ||
                                   strcasecmp(value, "true")    == 0 ||
                                   strcasecmp(value, "1")       == 0 ||
                                   strcasecmp(value, "enabled") == 0) ? 1 : 0;
        }
        else if (strcasecmp(key, "UPLOAD_DIR") == 0) {
            /*
             * Directory where uploaded files are saved.
             * Must exist and be writable by the tg-bot user before use.
             * Falls back to default if value is too long.
             */
            if (safe_copy(cfg->upload_dir, sizeof(cfg->upload_dir), value) != 0) {
                LOG_CFG(LOG_WARN, "UPLOAD_DIR too long, using default");
                safe_copy(cfg->upload_dir, sizeof(cfg->upload_dir), "/var/www/html/uploads");
            }
        }
        else if (strcasecmp(key, "SSH_KEYS_PATH") == 0) {
            /*
             * Path to authorized_keys file for /sshkeys command.
             * Feature is disabled when empty — no default path is assumed
             * since the owning user varies per system.
             */
            if (safe_copy(cfg->ssh_keys_path, sizeof(cfg->ssh_keys_path), value) != 0) {
                LOG_CFG(LOG_WARN, "SSH_KEYS_PATH too long — /sshkeys disabled");
                cfg->ssh_keys_path[0] = '\0';
            }
        }
        else if (strcasecmp(key, "SUDO_PATH") == 0) {
            safe_copy(cfg->sudo_path, sizeof(cfg->sudo_path), value);
        }
        else if (strcasecmp(key, "SYSTEMCTL_PATH") == 0) {
            safe_copy(cfg->systemctl_path, sizeof(cfg->systemctl_path), value);
        }
        else if (strcasecmp(key, "JOURNALCTL_PATH") == 0) {
            safe_copy(cfg->journalctl_path, sizeof(cfg->journalctl_path), value);
        }
        else if (strcasecmp(key, "F2B_WRAPPER_PATH") == 0) {
            safe_copy(cfg->f2b_wrapper_path, sizeof(cfg->f2b_wrapper_path), value);
        }
        else {
            LOG_CFG(LOG_DEBUG, "Unknown config key: %s", key);
        }
    }

    /* fclose before validation — file is fully parsed at this point */
    fclose(f);

    /* Validate required fields */
    if (cfg->token[0] == '\0') {
        LOG_CFG(LOG_ERROR, "TOKEN is missing");
        return -1;
    }

    if (cfg->chat_id == 0) {
        LOG_CFG(LOG_ERROR, "CHAT_ID is missing or invalid");
        return -1;
    }

    /*
     * If upload is enabled but upload_dir is empty — disable upload with
     * a warning rather than crashing. Bot continues to work normally,
     * just without file upload support.
     */
    if (cfg->upload_enabled && cfg->upload_dir[0] == '\0') {
        LOG_CFG(LOG_WARN,
            "UPLOAD_ENABLED=yes but UPLOAD_DIR is empty — upload disabled");
        cfg->upload_enabled = 0;
    }

    LOG_CFG(LOG_INFO, "Config loaded (log_level=%d)", cfg->log_level);
    LOG_CFG(LOG_DEBUG, "parsed log_level=%s", logger_level_to_string(cfg->log_level));

    return 0;
}

// ============================================================================
// UTILITIES
// ============================================================================

void config_log(const config_t *cfg) {
    LOG_CFG(LOG_INFO, "===== CONFIG =====");
    LOG_CFG(LOG_INFO, "CHAT_ID: %ld",    cfg->chat_id);
    LOG_CFG(LOG_INFO, "TOKEN_TTL: %d",   cfg->token_ttl);
    LOG_CFG(LOG_INFO, "LOG_FILE: %s",    cfg->log_file);
    LOG_CFG(LOG_INFO, "LOG_LEVEL: %s",   logger_level_to_string(cfg->log_level));
    LOG_CFG(LOG_INFO, "TOTP: %s",
            cfg->totp_secret[0] != '\0' ? "enabled" : "disabled (token flow)");
    LOG_CFG(LOG_INFO, "TOTP_SETUP: %s",
            cfg->totp_setup_enabled ? "enabled" : "disabled");
    LOG_CFG(LOG_INFO, "UPLOAD: %s%s",
            cfg->upload_enabled ? "enabled" : "disabled",
            cfg->upload_enabled ? cfg->upload_dir[0] != '\0'
                ? "" : " (no dir!)" : "");
    LOG_CFG(LOG_INFO, "SSH_KEYS_PATH: %s",
            cfg->ssh_keys_path[0] != '\0' ? cfg->ssh_keys_path : "disabled");

    /* Utility paths — useful for diagnosing permission or path issues */
    LOG_CFG(LOG_DEBUG, "SUDO_PATH: %s",        cfg->sudo_path);
    LOG_CFG(LOG_DEBUG, "SYSTEMCTL_PATH: %s",   cfg->systemctl_path);
    LOG_CFG(LOG_DEBUG, "JOURNALCTL_PATH: %s",  cfg->journalctl_path);
    LOG_CFG(LOG_DEBUG, "F2B_WRAPPER_PATH: %s", cfg->f2b_wrapper_path);
    LOG_CFG(LOG_DEBUG, "UPLOAD_DIR: %s",       cfg->upload_dir);
}

// ============================================================================
// CONFIGURATION RELOAD
// ============================================================================

/**
 * Reload configuration file (called on SIGHUP)
 *
 * Loads new configuration from disk and applies changes to:
 *   - Log file (reopens if path changed)
 *   - Log level
 *   - Allowed chat ID
 *   - Token TTL
 *   - TOTP secret
 *   - Upload enabled flag and directory
 *   - SSH keys path
 *
 * @param path  Path to configuration file
 * @param cfg   Pointer to current config (updated in-place on success)
 * @return      0 on success, -1 on error (original config unchanged)
 */
int config_reload(const char *path, config_t *cfg) {
    LOG_STATE(LOG_INFO, "Reload config");

    config_t new_cfg;
    if (config_load(path, &new_cfg) != 0)
        return -1;

    if (strcmp(cfg->log_file, new_cfg.log_file) != 0)
        logger_reopen(new_cfg.log_file);

    logger_set_level(new_cfg.log_level);
    security_set_allowed_chat(new_cfg.chat_id);
    security_set_token_ttl(new_cfg.token_ttl);

    *cfg = new_cfg;
    config_log(cfg);

    return 0;
}
