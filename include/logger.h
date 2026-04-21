/**
 * tg-bot - Telegram bot for system administration
 * logger.h - Thread-safe logging subsystem
 * MIT License - Copyright (c) 2026
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

// ============================================================================
// LOG LEVELS
// ============================================================================

/**
 * Log verbosity levels (ordered by increasing severity)
 */
typedef enum {
    LOG_ERROR = 0,  // Critical errors, program may not continue
    LOG_WARN,       // Warnings, something unexpected but recoverable
    LOG_INFO,       // Informational messages, normal operation
    LOG_DEBUG       // Debug messages, verbose troubleshooting
} log_level_t;

// ============================================================================
// INITIALIZATION AND SHUTDOWN
// ============================================================================

/**
 * Initialize logger with specified log file path
 * 
 * Opens the log file in append mode. Falls back to stderr if file cannot be opened.
 * 
 * @param path  Path to log file
 * @return      0 on success, -1 on failure (fallback to stderr enabled)
 */
int logger_init(const char *path);

/**
 * Close logger and release resources
 * 
 * Flushes any buffered data and closes the log file.
 * Re-enables stderr fallback for subsequent log attempts.
 */
void logger_close(void);

// ============================================================================
// CONFIGURATION
// ============================================================================

/**
 * Set minimum log level for output
 * 
 * Messages with level > current_level will be suppressed.
 * Example: logger_set_level(LOG_WARN) shows only WARN and ERROR.
 * 
 * @param level  New log level threshold
 */
void logger_set_level(log_level_t level);

/**
 * Convert log level enum to human-readable string
 * 
 * @param level  Log level to convert
 * @return       String representation (e.g., "INFO ", "DEBUG")
 */
const char *logger_level_to_string(log_level_t level);

// ============================================================================
// CORE LOGGING FUNCTION
// ============================================================================

/**
 * Write a log message (internal core function)
 * 
 * This is the primary logging entry point. All LOG_* macros eventually call this.
 * 
 * Features:
 *   - Timestamp with millisecond precision
 *   - Log level filtering
 *   - Thread-safe writes via mutex
 *   - Automatic fallback to stderr
 *   - Truncation protection for oversized messages
 * 
 * @param level  Log level of this message
 * @param fmt    printf-style format string
 * @param ...    Variable arguments for format string
 */
void log_msg(log_level_t level, const char *fmt, ...);

// ============================================================================
// MODULE TAG MACROS
// ============================================================================

/**
 * Configuration module logging
 * Usage: LOG_CFG(LOG_INFO, "Config loaded from %s", path);
 */
#define LOG_CFG(level, fmt, ...) \
    log_msg(level, "[CFG] " fmt, ##__VA_ARGS__)

/**
 * Network/Telegram module logging
 * Usage: LOG_NET(LOG_DEBUG, "Sending message to chat_id=%ld", chat_id);
 */
#define LOG_NET(level, fmt, ...) \
    log_msg(level, "[NET] " fmt, ##__VA_ARGS__)

/**
 * Security module logging
 * Usage: LOG_SEC(LOG_WARN, "Access denied for chat_id=%ld", chat_id);
 */
#define LOG_SEC(level, fmt, ...) \
    log_msg(level, "[SEC] " fmt, ##__VA_ARGS__)

/**
 * System module logging
 * Usage: LOG_SYS(LOG_INFO, "Process started (PID=%d)", getpid());
 */
#define LOG_SYS(level, fmt, ...) \
    log_msg(level, "[SYS] " fmt, ##__VA_ARGS__)

/**
 * State/lifecycle logging
 * Usage: LOG_STATE(LOG_INFO, "Bot started");
 */
#define LOG_STATE(level, fmt, ...) \
    log_msg(level, "[STATE] " fmt, ##__VA_ARGS__)

/**
 * Command handling logging
 * Usage: LOG_CMD(LOG_INFO, "Processing command: %s", cmd);
 */
#define LOG_CMD(level, fmt, ...) \
    log_msg(level, "[CMD] " fmt, ##__VA_ARGS__)

/**
 * External command execution logging
 * Usage: LOG_EXEC(LOG_DEBUG, "Executing: %s", cmdline);
 */
#define LOG_EXEC(level, fmt, ...) \
    log_msg(level, "[EXEC] " fmt, ##__VA_ARGS__)

/**
 * Execution check logging (healthcheck/startup checks)
 * Used for quiet diagnostic checks
 */
#define LOG_EXEC_CHECK(level, fmt, ...) \
    log_msg(level, "[EXECCHK] " fmt, ##__VA_ARGS__)

/**
 * Fallback logging (when logger itself fails)
 * Writes directly to stderr, bypassing normal logging.
 */
#define LOG_FALLBACK(level, fmt, ...) \
    fprintf(stderr, "[%s] [SYS] " fmt "\n", \
        logger_level_to_string(level), ##__VA_ARGS__)

// ============================================================================
// CONTEXT LOGGING MACROS
// ============================================================================

/**
 * Context logging helper (internal)
 * 
 * Appends request context to log message: req_id, chat_id, user_id, username.
 * Used by LOG_*_CTX macros below.
 */
#define LOG_CTX(LOG_MACRO, ctx, level, fmt, ...) \
    LOG_MACRO(level, fmt " (req=%04x chat_id=%ld user_id=%ld%s%s)", \
        ##__VA_ARGS__, \
        (ctx)->req_id, \
        (ctx)->chat_id, \
        (long)(ctx)->user_id, \
        ((ctx)->username) ? " user=@" : "", \
        ((ctx)->username) ? (ctx)->username : "")

/**
 * Network logging with context
 * Usage: LOG_NET_CTX(ctx, LOG_INFO, "Message received");
 */
#define LOG_NET_CTX(ctx, level, fmt, ...) \
    LOG_CTX(LOG_NET, ctx, level, fmt, ##__VA_ARGS__)

/**
 * Command logging with context
 * Usage: LOG_CMD_CTX(ctx, LOG_INFO, "Command executed");
 */
#define LOG_CMD_CTX(ctx, level, fmt, ...) \
    LOG_CTX(LOG_CMD, ctx, level, fmt, ##__VA_ARGS__)

/**
 * State logging with context
 * Usage: LOG_STATE_CTX(ctx, LOG_INFO, "State changed");
 */
#define LOG_STATE_CTX(ctx, level, fmt, ...) \
    LOG_CTX(LOG_STATE, ctx, level, fmt, ##__VA_ARGS__)

/**
 * Security logging with context
 * Usage: LOG_SEC_CTX(ctx, LOG_WARN, "Access check");
 */
#define LOG_SEC_CTX(ctx, level, fmt, ...) \
    LOG_CTX(LOG_SEC, ctx, level, fmt, ##__VA_ARGS__)

/**
 * System logging with context
 * Usage: LOG_SYS_CTX(ctx, LOG_ERROR, "System call failed");
 */
#define LOG_SYS_CTX(ctx, level, fmt, ...) \
    LOG_CTX(LOG_SYS, ctx, level, fmt, ##__VA_ARGS__)

#endif // LOGGER_H
