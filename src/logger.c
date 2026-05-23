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
 *   - Early message buffer: messages logged before logger_init() are
 *     buffered in memory and flushed to the log file on first open
 *   - Mirror to stderr when running interactively (isatty detection)
 *   - Line buffering for real-time log visibility
 *   - Truncation protection for oversized messages
 *
 * Log format: [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [TAG] message
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "logger.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

// ============================================================================
// CONSTANTS
// ============================================================================

/*
 * Early message buffer capacity.
 * Holds messages logged before the log file is opened (during startup,
 * before logger_init() succeeds). All buffered messages are flushed to
 * the log file on first successful open.
 *
 * 32 messages of 512 bytes each = 16 KB maximum — negligible.
 * Startup typically generates < 15 messages before the file opens.
 */
#define EARLY_LOG_MAX_MSGS  32
#define EARLY_LOG_MSG_SIZE  512

// ============================================================================
// INTERNAL STATE
// ============================================================================

/* Primary log file handle (NULL if not open) */
static FILE *log_file = NULL;

/* Current log level threshold (messages above this level are filtered) */
static log_level_t current_log_level = LOG_INFO;

/* Mutex for thread-safe writes */
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Mirror flag: write to stderr in addition to the log file.
 * Set automatically in logger_init() via isatty(STDERR_FILENO).
 * True  — running interactively (manual invocation from terminal).
 * False — running as a systemd service (stderr is redirected to a file).
 *
 * This prevents double entries in the log file when
 * StandardError= points to the same file as the logger.
 */
static int log_mirror_stderr = 0;

/*
 * Early message buffer.
 *
 * Messages logged before the log file is opened are stored here
 * (already formatted as complete "[timestamp] [LEVEL] text" lines).
 * They are also written to stderr immediately so nothing is lost.
 *
 * On the first successful logger_init() all buffered lines are
 * replayed into the newly opened file so the log is complete.
 */
static char early_log_buf[EARLY_LOG_MAX_MSGS][EARLY_LOG_MSG_SIZE];
static int  early_log_count = 0;

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
        case LOG_DEBUG: return "DEBUG ";
        case LOG_INFO:  return " INFO ";
        case LOG_WARN:  return " WARN ";
        case LOG_ERROR: return "ERROR ";
        default:        return "UNKWN ";
    }
}

// ============================================================================
// TIMESTAMP GENERATION (THREAD-SAFE, MILLISECOND PRECISION)
// ============================================================================

/**
 * Generate formatted timestamp with millisecond precision.
 *
 * Uses a single clock_gettime(CLOCK_REALTIME) call to obtain both the
 * seconds and nanoseconds, then derives the broken-down time via
 * localtime_r(). This avoids the race condition that would occur if
 * time() and clock_gettime() were called separately (the second could
 * roll over between the two calls).
 *
 * Format: YYYY-MM-DD HH:MM:SS.mmm
 *
 * @param buf   Output buffer
 * @param size  Size of output buffer
 */
static void get_timestamp(char *buf, size_t size) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm_info;
    if (localtime_r(&ts.tv_sec, &tm_info) == NULL) {
        snprintf(buf, size, "0000-00-00 00:00:00.000");
        return;
    }

    strftime(buf, size, "%Y-%m-%d %H:%M:%S", &tm_info);

    size_t len = strlen(buf);
    if (len < size)
        snprintf(buf + len, size - len, ".%03ld", ts.tv_nsec / 1000000);
}

// ============================================================================
// INITIALIZATION AND SHUTDOWN
// ============================================================================

/**
 * Initialize logger with specified log file path.
 *
 * Opens the log file in append mode.  On success:
 *   - All messages buffered in early_log_buf are flushed to the file
 *     so the log is complete from process start.
 *   - log_mirror_stderr is set via isatty(STDERR_FILENO): true for
 *     interactive runs, false for systemd service (prevents duplicates).
 *
 * On failure falls back to writing directly to stderr.
 *
 * @param path  Path to log file
 * @return      0 on success, -1 on failure (stderr fallback active)
 */
int logger_init(const char *path) {
    if (!path) {
        LOG_FALLBACK(LOG_ERROR, "logger init failed: NULL path, using stderr");
        return -1;
    }

    log_file = fopen(path, "a");
    if (!log_file) {
        LOG_FALLBACK(LOG_ERROR,
            "logger init failed: cannot open %s, using stderr", path);
        return -1;
    }

    /* Line buffering for real-time log visibility */
    setvbuf(log_file, NULL, _IOLBF, 0);

    /*
     * Flush early buffer — replay messages that were logged before this
     * file was opened. Written without the mutex because we are still
     * single-threaded at this point in main().
     */
    for (int i = 0; i < early_log_count; i++)
        fprintf(log_file, "%s", early_log_buf[i]);

    fflush(log_file);
    early_log_count = 0;   /* buffer consumed — reset for safety */

    /*
     * Mirror to stderr only when running interactively.
     * isatty() returns 1 if stderr is a real terminal (./tg-bot run by hand),
     * 0 if it is redirected (systemd unit, CI pipeline, pipe).
     */
    log_mirror_stderr = isatty(STDERR_FILENO);

    return 0;
}

/**
 * Set minimum log level for output.
 *
 * Messages with level > current_level are suppressed.
 *
 * @param level  New log level threshold
 */
void logger_set_level(log_level_t level) {
    current_log_level = level;
}

/**
 * Close logger and release resources.
 *
 * Flushes all buffered data and closes the log file.
 */
void logger_close(void) {
    if (log_file) {
        fflush(log_file);
        fclose(log_file);
        log_file = NULL;
    }
    log_mirror_stderr = 0;
}

// ============================================================================
// MAIN LOGGING FUNCTION
// ============================================================================

/**
 * Write a log message (internal core function).
 *
 * All LOG_* macros call this function.  Behaviour:
 *
 *   log file not yet open (early startup)
 *     → store formatted line in early_log_buf (replayed on logger_init)
 *     → always write to stderr so nothing is lost at the console
 *
 *   log file open, interactive run (isatty = 1)
 *     → write to log file
 *     → mirror to stderr  (user sees output in terminal)
 *
 *   log file open, systemd service (isatty = 0)
 *     → write to log file only  (no stderr duplication)
 *
 * @param level  Log level of this message
 * @param fmt    printf-style format string
 * @param ...    Variable arguments for format string
 */
void log_msg(log_level_t level, const char *fmt, ...) {
    if (level > current_log_level)
        return;

    if (!fmt) return;

    /* Generate timestamp */
    char timestamp[40];
    get_timestamp(timestamp, sizeof(timestamp));

    /* Format the log message */
    char message[1024];
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    if (written < 0) {
        strncpy(message, "log formatting error", sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
    } else if ((size_t)written >= sizeof(message)) {
        message[sizeof(message) - 4] = '.';
        message[sizeof(message) - 3] = '.';
        message[sizeof(message) - 2] = '.';
        message[sizeof(message) - 1] = '\0';
    }

    /* Assemble the complete log line once — reused for file, stderr, buffer */
    char line[EARLY_LOG_MSG_SIZE];
    snprintf(line, sizeof(line), "[%s] [%s] %s\n",
             timestamp, logger_level_to_string(level), message);

    // ------------------------------------------------------------------------
    // Critical section
    // ------------------------------------------------------------------------
    pthread_mutex_lock(&log_mutex);

    if (!log_file) {
        /*
         * Log file not open yet (early startup phase).
         * Buffer the line so it can be replayed into the file later,
         * and always echo to stderr so the operator sees it immediately.
         */
        if (early_log_count < EARLY_LOG_MAX_MSGS)
            safe_copy(early_log_buf[early_log_count++], EARLY_LOG_MSG_SIZE, line);

        fprintf(stderr, "%s", line);
        fflush(stderr);

        pthread_mutex_unlock(&log_mutex);
        return;
    }

    /* Write to log file; flush only on success (_IOLBF handles most cases) */
    if (fprintf(log_file, "%s", line) < 0) {
        fprintf(stderr, "[LOGGER ERROR] write to file failed\n");
    } else {
        fflush(log_file);
    }

    /*
     * Mirror to stderr when running interactively (isatty = 1).
     * Skipped for systemd service to prevent double entries when
     * StandardError= redirects stderr to the same log file.
     */
    if (log_mirror_stderr) {
        fprintf(stderr, "%s", line);
        fflush(stderr);
    }

    pthread_mutex_unlock(&log_mutex);
}

// ============================================================================
// UTILITIES
// ============================================================================

/**
 * Reopen log file — used after rotation or config path change.
 *
 * Verifies write access via a temporary fopen() (more reliable than
 * access() which cannot detect whether a new file can be created),
 * flushes all buffers, closes the current log file and reopens via
 * logger_init().
 *
 * @param path  Path to new log file
 * @return      0 on success, -1 on error (file not writable/creatable)
 */
int logger_reopen(const char *path) {
    /* Probe writability before closing the current log */
    FILE *f = fopen(path, "a");
    if (!f) return -1;
    fclose(f);

    fflush(NULL);
    logger_close();
    return logger_init(path);
}
