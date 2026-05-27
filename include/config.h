/**
 * tg-bot - Telegram bot for system administration
 * config.h - Configuration file parsing
 * MIT License - Copyright (c) 2026 ironmist45
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>
#include "logger.h"
#include "totp.h"

// ============================================================================
// TYPES
// ============================================================================

/**
 * Bot configuration structure
 *
 * Populated by config_load() from a key=value file.
 * Required fields: TOKEN, CHAT_ID
 * Optional fields have sensible defaults.
 */
typedef struct {
    /* Required */
    char token[128];            /* Telegram Bot API token                    */
    long chat_id;               /* Allowed Telegram chat ID (single-user)    */

    /* Optional (with defaults) */
    char log_file[256];         /* Path to log file (default: /var/log/tg-bot.log) */
    int  token_ttl;             /* Confirmation token TTL in seconds (default: 60) */
    log_level_t log_level;      /* Logging verbosity (default: LOG_INFO)     */

    /*
     * TOTP secret for two-step confirmation (optional).
     *
     * Base32-encoded shared secret compatible with Google Authenticator,
     * Authy, and any RFC 6238 TOTP application.
     *
     * When set:   /reboot and /restart require a TOTP code from the app.
     * When empty: classic stateless token flow is used (default behaviour).
     *
     * To generate a secret:
     *   openssl rand -base32 20
     *
     * To get the otpauth:// URI for QR scanning:
     *   send /totp_setup to the bot after configuring the secret.
     *
     * Example:
     *   TOTP_SECRET=JBSWY3DPEHPK3PXP
     */
    char totp_secret[TOTP_SECRET_MAX];  /* Base32 TOTP secret, empty = disabled */

    /*
     * TOTP setup command visibility (optional).
     *
     * When 0 (default): /totp_setup shows secret and URI only if
     *   TOTP_SECRET is not yet configured. Once secret is set,
     *   /totp_setup returns "Setup disabled" to prevent secret exposure.
     *
     * When 1: /totp_setup always shows secret and URI.
     *   Use only during initial setup, then disable.
     *
     * Config key: TOTP_SETUP=enabled / disabled
     * Default: disabled (0)
     */
    int totp_setup_enabled;     /* 1 = show URI, 0 = hide after setup        */

    /* System paths (with defaults) */
    char sudo_path[128];
    char systemctl_path[128];
    char journalctl_path[128];
    char f2b_wrapper_path[128];

    /*
     * File upload feature (optional).
     *
     * upload_enabled: controls whether the bot accepts incoming files.
     *   When 0 (default): incoming files are silently ignored, /files
     *     returns "File upload is disabled".
     *   When 1: files sent to the bot are downloaded from Telegram and
     *     saved to upload_dir.
     *
     * Config key: UPLOAD_ENABLED=yes/true/1/enabled
     * Default: disabled (0)
     *
     * upload_dir: directory where received files are saved.
     *   Must exist and be writable by the tg-bot user before use.
     *   If upload_enabled=1 but upload_dir is empty, upload is
     *   automatically disabled with a warning at startup.
     *
     *   Create before starting the bot:
     *     mkdir -p /var/www/html/uploads
     *     chown tg-bot:tg-bot /var/www/html/uploads
     *
     * Config key: UPLOAD_DIR=/var/www/html/uploads
     * Default: /var/www/html/uploads
     */
    int  upload_enabled;        /* 1 = accept incoming files, 0 = ignore     */
    char upload_dir[256];       /* Destination directory for uploaded files  */

    /*
     * SSH authorized_keys file path (optional).
     *
     * When set:   /sshkeys reads the file and displays key type and comment
     *   for each entry — the key itself is not shown.
     * When empty: /sshkeys returns a configuration hint (default).
     *
     * File is read via 'sudo cat' since the bot runs as tg-bot,
     * not as the user who owns authorized_keys.
     *
     * Requires a sudoers entry for the tg-bot user, e.g.:
     *   tg-bot ALL=(ALL) NOPASSWD: /bin/cat /home/user/.ssh/authorized_keys
     *
     * No default path is assumed — the owning user varies per system.
     *
     * Config key: SSH_KEYS_PATH=/home/user/.ssh/authorized_keys
     * Default: empty (feature disabled)
     */
    char ssh_keys_path[512];    /* Path to authorized_keys, empty = disabled */

} config_t;

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Load and parse configuration file
 *
 * File format: KEY=VALUE pairs, case-insensitive keys.
 * Lines starting with '#' are treated as comments.
 * Empty lines are ignored.
 *
 * Supported keys:
 *   TOKEN          - Telegram Bot API token (required)
 *   CHAT_ID        - Allowed Telegram chat ID (required)
 *   LOG_FILE       - Path to log file (default: /var/log/tg-bot.log)
 *   TOKEN_TTL      - Confirmation token TTL in seconds (default: 60)
 *   LOG_LEVEL      - Logging level: ERROR, WARN, INFO, DEBUG (default: INFO)
 *   TOTP_SECRET    - Base32 TOTP secret for 2FA (optional, default: disabled)
 *   UPLOAD_ENABLED - Enable file upload: yes/true/1/enabled (default: disabled)
 *   UPLOAD_DIR     - Directory for uploaded files (default: /var/www/html/uploads)
 *   SSH_KEYS_PATH  - Path to authorized_keys file (optional, default: disabled)
 *
 * Example:
 *   TOKEN=1234567890:ABCdefGHIjklMNOpqrsTUVwxyz
 *   CHAT_ID=123456789
 *   LOG_LEVEL=DEBUG
 *   TOTP_SECRET=JBSWY3DPEHPK3PXP
 *   UPLOAD_ENABLED=yes
 *   UPLOAD_DIR=/var/www/html/uploads
 *   SSH_KEYS_PATH=/home/user/.ssh/authorized_keys
 *
 * @param path  Path to configuration file
 * @param cfg   Output structure to populate
 * @return      0 on success, -1 on error
 */
int config_load(const char *path, config_t *cfg);

/**
 * Log current configuration (for debugging)
 * @param cfg Pointer to config structure
 */
void config_log(const config_t *cfg);

/**
 * Reload configuration file (for SIGHUP)
 * @param path Path to config file
 * @param cfg Pointer to config structure (updated in-place)
 * @return 0 on success, -1 on error
 */
int config_reload(const char *path, config_t *cfg);

/* Global config instance (defined in main.c) */
extern config_t g_cfg;

#endif /* CONFIG_H */
