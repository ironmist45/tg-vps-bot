/**
 * tg-bot - Telegram bot for system administration
 * logstat.h - Bot log file statistics analyzer
 * MIT License - Copyright (c) 2026 ironmist45
 */

#ifndef LOGSTAT_H
#define LOGSTAT_H

#include <stddef.h>

// ============================================================================
// CONSTANTS
// ============================================================================

/* Maximum log file size to analyze (4MB) */
#define LOGSTAT_MAX_SIZE     (4 * 1024 * 1024)

/* Number of hours in a day */
#define LOGSTAT_HOURS        24

/* Maximum tag name length */
#define LOGSTAT_TAG_MAX_LEN  16

/* Number of tracked tags */
#define LOGSTAT_TAG_COUNT    7

// ============================================================================
// TYPES
// ============================================================================

/**
 * Per-tag statistics
 */
typedef struct {
    const char *name;   /* Tag name e.g. "NET", "SYS" */
    long        count;  /* Number of lines with this tag */
} logstat_tag_t;

/**
 * Full log statistics result
 */
typedef struct {
    long total_lines;                       /* Total lines in log */
    long level_error;                       /* Lines with [ERROR ] */
    long level_warn;                        /* Lines with [ WARN ] */
    long level_info;                        /* Lines with [ INFO ] */
    long level_debug;                       /* Lines with [DEBUG ] */
    long level_other;                       /* Lines with unknown level */
    logstat_tag_t tags[LOGSTAT_TAG_COUNT];  /* Per-tag counts */
    long hour_counts[LOGSTAT_HOURS];        /* Lines per hour (0-23) */
    long busiest_hour;                      /* Hour with most lines */
    size_t file_size;                       /* Log file size in bytes */
} logstat_result_t;

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Analyze bot log file and populate result structure.
 *
 * Uses SSE4.2 inline asm for fast newline counting,
 * scalar pass for level/tag/hour extraction.
 *
 * @param path    Path to log file
 * @param result  Output statistics structure
 * @return        0 on success, -1 on error
 */
int logstat_analyze(const char *path, logstat_result_t *result);

/**
 * Format statistics into Telegram-ready string.
 *
 * @param result  Populated statistics structure
 * @param buf     Output buffer
 * @param size    Buffer size
 * @return        0 on success, -1 on error
 */
int logstat_format(const logstat_result_t *result, char *buf, size_t size);

#endif /* LOGSTAT_H */
