/**
 * tg-bot - Telegram bot for system administration
 * 
 * logger.c - Thread-safe logging subsystem
 * 
 * Provides structured logging with the following features:
 *   - Timestamps with millisecond precision
 *   - Log level filtering (ERROR, WARN, INFO, DEBUG)
 *   - Thread-safe writes via pthread mutex
 *   - Automatic fallback to stderr if log file unavailable
 *   - Line buffering for real-time log visibility
 *   - Truncation protection for oversized messages
 * 
 * Log format: [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [TAG] message
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

#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

// ============================================================================
// INTERNAL STATE
// ============================================================================

// Primary log file handle (NULL if not open)
static FILE *log_file = NULL;

// Fallback flag: write to stderr if log file unavailable
static int log_to_stderr = 0;

// Current log level threshold (messages with higher level are filtered)
static log_level_t current_log_level = LOG_INFO;

// Mutex for thread-safe writes
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// ============================================================================
// LEVEL TO STRING CONVERSION
// ============================================================================

/**
 * Convert log level enum to human-readable string
 * 
 * @param level  Log level to convert
 * @return       String representation (e.g., "INFO ", "DEBUG")
 */
const char *logger_level_to_string(log_level_t level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO ";
        case LOG_WARN:  return "WARN ";
        case LOG_ERROR: return "ERROR";
        default:        return "UNKWN";
    }
}

// ============================================================================
// TIMESTAMP GENERATION (THREAD-SAFE, MILLISECOND PRECISION)
// ============================================================================

/**
 * Generate formatted timestamp with millisecond precision
 * 
 * Format: YYYY-MM-DD HH:MM:SS.mmm
 * Thread-safe: uses localtime_r() and clock_gettime()
 * 
 * @param buf   Output buffer
 * @param size  Size of output buffer
 */
static void get_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm tm_info;

    if (localtime_r(&now, &tm_info) == NULL) {
        snprintf(buf, size, "0000-00-00 00:00:00.000");
        return;
    }

    // Format date and time (without milliseconds)
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tm_info);

    // Append milliseconds
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    size_t len = strlen(buf);
    if (len < size) {
        snprintf(buf + len, size - len, ".%03ld",
                 ts.tv_nsec / 1000000);
    }
}

// ============================================================================
// INITIALIZATION AND SHUTDOWN
// ============================================================================

/**
 * Initialize logger with specified log file path
 * 
 * Opens the log file in append mode. If the file cannot be opened,
 * falls back to writing logs to stderr.
 * 
 * @param path  Path to log file
 * @return      0 on success, -1 on failure (fallback to stderr enabled)
 */
int logger_init(const char *path) {

    // Validate path parameter
    if (!path) {
        log_to_stderr = 1;
        LOG_FALLBACK(LOG_ERROR,
            "logger init failed: NULL path, using stderr");
        return -1;
    }

    // Attempt to open log file
    log_file = fopen(path, "a");

    if (!log_file) {
        // Fallback to stderr if file cannot be opened
        log_to_stderr = 1;
        LOG_FALLBACK(LOG_ERROR,
            "logger init failed: cannot open %s, using stderr", path);
        return -1;
    }

    log_to_stderr = 0;

    // Use line buffering for real-time log visibility
    setvbuf(log_file, NULL, _IOLBF, 0);

    return 0;
}

/**
 * Set minimum log level for output
 * 
 * Messages with level > current_level will be suppressed.
 * 
 * @param level  New log level threshold
 */
void logger_set_level(log_level_t level) {
    current_log_level = level;
}

/**
 * Close logger and release resources
 * 
 * Flushes any buffered data and closes the log file.
 * Enables stderr fallback for any subsequent log attempts.
 */
void logger_close() {
    if (log_file) {
        fflush(log_file);  // Ensure all data is written
        fclose(log_file);
        log_file = NULL;
    }

    // Re-enable stderr fallback
    log_to_stderr = 1;
}

// ============================================================================
// MAIN LOGGING FUNCTION
// ============================================================================

/**
 * Write a log message (internal core function)
 * 
 * This is the primary logging entry point. All LOG_* macros eventually
 * call this function. It handles:
 *   - Level filtering
 *   - Timestamp generation
 *   - Message formatting (printf-style)
 *   - Thread-safe writes to file and/or stderr
 *   - Truncation of oversized messages
 * 
 * @param level  Log level of this message
 * @param fmt    printf-style format string
 * @param ...    Variable arguments for format string
 */
void log_msg(log_level_t level, const char *fmt, ...) {

    // Filter messages below current log level
    if (level > current_log_level) {
        return;
    }

    // Protect against NULL format string
    if (!fmt) return;

    FILE *file_out = log_file;
    FILE *console_out = stderr;

    // If neither file nor stderr fallback is available, drop the message
    if (!file_out && !log_to_stderr) {
        return;
    }

    // Generate timestamp
    char timestamp[40];
    get_timestamp(timestamp, sizeof(timestamp));

    // Format the log message
    char message[1024];

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    // Handle formatting errors
    if (written < 0) {
        strncpy(message, "log formatting error", sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
    }
    // Handle truncation: add "..." at the end
    else if ((size_t)written >= sizeof(message)) {
        size_t len = sizeof(message);
        if (len > 4) {
            message[len - 4] = '.';
            message[len - 3] = '.';
            message[len - 2] = '.';
            message[len - 1] = '\0';
        }
    }

    // ------------------------------------------------------------------------
    // Critical section: ensure atomic writes
    // ------------------------------------------------------------------------
    pthread_mutex_lock(&log_mutex);

    // Write to log file (if available)
    if (file_out) {
        if (fprintf(file_out, "[%s] [%s] %s\n",
                    timestamp,
                    logger_level_to_string(level),
                    message) < 0) {
            // Notify about write failure (to stderr, bypassing normal logging)
            fprintf(stderr, "[LOGGER ERROR] write to file failed\n");
        }

        fflush(file_out);
    }

    // Write to stderr as fallback (if file unavailable)
    if (!file_out && log_to_stderr) {
        fprintf(stderr, "[%s] [%s] %s\n",
                timestamp,
                logger_level_to_string(level),
                message);

        fflush(stderr);
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    // Duplicate to console if available (for debugging)
    if (console_out) {
        fprintf(console_out, "[%s] [%s] %s\n",
                timestamp,
                logger_level_to_string(level),
                message);

        fflush(console_out);
    }

    pthread_mutex_unlock(&log_mutex);
}

// ============================================================================
// UTILITIES
// ============================================================================

int logger_reopen(const char *path) {
    FILE *f = fopen(path, "a");
    if (!f) return -1;
    fclose(f);

    fflush(NULL);
    logger_close();
    return logger_init(path);
}
