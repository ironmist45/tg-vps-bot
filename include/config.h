/**
 * tg-bot - Telegram bot for system administration
 * config.h - Configuration file parsing
 * MIT License - Copyright (c) 2026
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>
#include "logger.h"

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
    // Required
    char token[128];        // Telegram Bot API token
    long chat_id;           // Allowed Telegram chat ID (single-user mode)
    
    // Optional (with defaults)
    int poll_timeout;       // Long polling timeout in seconds (default: 30)
    char log_file[256];     // Path to log file (default: /var/log/tg-bot.log)
    int token_ttl;          // Reboot token TTL in seconds (default: 60)
    log_level_t log_level;  // Logging verbosity (default: LOG_INFO)
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
 *   TOKEN        - Telegram Bot API token (required)
 *   CHAT_ID      - Allowed Telegram chat ID (required)
 *   POLL_TIMEOUT - Long polling timeout in seconds (default: 30)
 *   LOG_FILE     - Path to log file (default: /var/log/tg-bot.log)
 *   TOKEN_TTL    - Reboot token time-to-live in seconds (default: 60)
 *   LOG_LEVEL    - Logging level: ERROR, WARN, INFO, DEBUG (default: INFO)
 * 
 * Example:
 *   TOKEN=1234567890:ABCdefGHIjklMNOpqrsTUVwxyz
 *   CHAT_ID=123456789
 *   LOG_LEVEL=DEBUG
 * 
 * @param path  Path to configuration file
 * @param cfg   Output structure to populate
 * @return      0 on success, -1 on error
 */
int config_load(const char *path, config_t *cfg);

// ============================================================================
// UTILITIES
// ============================================================================

void config_log(const config_t *cfg);

#endif // CONFIG_H
