/**
 * tg-bot - Telegram bot for system administration
 * logstat.c - Bot log file statistics analyzer
 *
 * Uses SSE4.2 inline asm for fast newline counting,
 * scalar pass for level/tag/hour extraction.
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#define _GNU_SOURCE

#include "logstat.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

/* Tag definitions — must match LOGSTAT_TAG_COUNT */
static const char *tag_names[LOGSTAT_TAG_COUNT] = {
    "NET", "SYS", "STATE", "CFG", "SEC", "CMD", "EXEC"
};

// ============================================================================
// SSE4.2 NEWLINE COUNTER
// ============================================================================

/**
 * Count newlines in buffer using SSE4.2 inline asm.
 *
 * Processes 16 bytes per iteration using PCMPISTRM instruction
 * (Packed Compare Implicit Length Strings, Return Mask).
 * Falls back to scalar loop for remaining bytes.
 *
 * PCMPISTRM with imm8=0x00 performs unsigned byte comparison,
 * equal-any mode — returns bitmask of matching positions.
 * POPCNT counts set bits in the mask.
 *
 * @param buf  Input buffer
 * @param len  Buffer length in bytes
 * @return     Number of newline characters
 */
static size_t count_newlines_sse42(const char *buf, size_t len)
{
    size_t count = 0;
    size_t i     = 0;

#if defined(__SSE4_2__) && defined(__x86_64__)
    /*
     * SSE4.2 path: process 16 bytes per iteration.
     *
     * xmm0 — needle: 16 bytes all set to '\n' (0x0A)
     * xmm1 — haystack: 16 bytes from input buffer
     *
     * PCMPISTRM imm8=0x00:
     *   - unsigned byte comparison
     *   - equal-any mode: each byte in xmm1 compared against all bytes in xmm0
     *   - result mask returned in xmm0
     *
     * PMOVMSKB extracts MSB of each byte → 16-bit integer mask
     * POPCNT counts matching positions
     */
    size_t sse_len = len & ~(size_t)15;  /* round down to 16-byte boundary */

    for (i = 0; i < sse_len; i += 16) {
        uint32_t mask = 0;

        __asm__ volatile (
            /* Load needle: 16 bytes of '\n' into xmm0 */
            "movd       %[nl],  %%xmm0          \n\t"
            "punpcklbw  %%xmm0, %%xmm0          \n\t"
            "punpcklwd  %%xmm0, %%xmm0          \n\t"
            "pshufd     $0,     %%xmm0, %%xmm0  \n\t"

            /* Load 16 bytes of input into xmm1 */
            "movdqu     (%[src]), %%xmm1         \n\t"

            /*
             * PCMPISTRM xmm0, xmm1, imm8=0x00
             *   imm8 = 0x00:
             *     bits[1:0] = 00 → unsigned bytes
             *     bits[3:2] = 00 → equal any
             *     bit[6]    = 0  → return mask in xmm0 (LSB format)
             */
            "pcmpistrm  $0x00,  %%xmm1, %%xmm0  \n\t"

            /* Extract 16-bit mask from xmm0 → eax */
            "pmovmskb   %%xmm0, %[mask]          \n\t"

            : [mask] "=r" (mask)
            : [nl]   "r"  ((uint32_t)'\n'),
              [src]  "r"  (buf + i)
            : "xmm0", "xmm1", "cc"
        );

        /* Count set bits (matching newlines) */
        __asm__ volatile (
            "popcnt %[m], %[m]"
            : [m] "+r" (mask)
        );

        count += mask;
    }
#endif

    /* Scalar fallback for remaining bytes (or entire buffer if no SSE4.2) */
    for (; i < len; i++) {
        if (buf[i] == '\n')
            count++;
    }

    return count;
}

// ============================================================================
// LINE PARSER
// ============================================================================

/**
 * Extract hour from log line timestamp.
 *
 * Expected format: [2026-05-06 14:32:11.123] ...
 * Hour is at fixed offset: '[' + 12 chars = hour position.
 *
 * @param line  Log line (not null-terminated, use len)
 * @param len   Line length
 * @return      Hour (0-23), or -1 if not parseable
 */
static int extract_hour(const char *line, size_t len)
{
    /* Minimum: "[2026-05-06 14:" = 15 chars */
    if (len < 15) return -1;
    if (line[0] != '[') return -1;

    /* Hour digits at offset 12-13: "[2026-05-06 HH:" */
    char h1 = line[12];
    char h2 = line[13];

    if (h1 < '0' || h1 > '2') return -1;
    if (h2 < '0' || h2 > '9') return -1;

    int hour = (h1 - '0') * 10 + (h2 - '0');
    return (hour >= 0 && hour < 24) ? hour : -1;
}

/**
 * Identify log level from line.
 *
 * Expected format: [timestamp] [LEVEL ] [TAG] message
 * Level field starts after first '] [' sequence.
 *
 * @param line  Log line
 * @param len   Line length
 * @return      0=error, 1=warn, 2=info, 3=debug, -1=unknown
 */
static int extract_level(const char *line, size_t len)
{
    /*
     * Format: [2026-05-06 14:32:11.123] [ INFO ] [ NET ] ...
     * Timestamp field is 25 chars: "[YYYY-MM-DD HH:MM:SS.mmm]"
     * Level field starts at offset 27: "[ INFO ]" or "[ERROR ]" etc.
     */
    if (len < 35) return -1;
    if (line[26] != '[') return -1;

    /* Check level at offset 26: "[ERROR ]" "[DEBUG ]" "[ INFO ]" "[ WARN ]" */
    const char *lp = line + 26;

    if (strncmp(lp, "[ERROR ]", 8) == 0) return 0;
    if (strncmp(lp, "[ WARN ]", 8) == 0) return 1;
    if (strncmp(lp, "[ INFO ]", 8) == 0) return 2;
    if (strncmp(lp, "[DEBUG ]", 8) == 0) return 3;

    return -1;
}

/**
 * Identify tag from log line.
 *
 * Tag field starts at offset 35: "[ TAG ]" or "[STATE]" etc.
 *
 * @param line  Log line
 * @param len   Line length
 * @return      Index into tag_names[], or -1 if not found
 */
static int extract_tag(const char *line, size_t len)
{
    /*
     * Format: [timestamp] [level ] [ TAG ] message
     * After "[timestamp] " (26) + "[level ] " (9) = offset 35
     */
    if (len < 43) return -1;
    if (line[35] != '[') return -1;

    const char *tp = line + 36;  /* skip '[' */

    for (int i = 0; i < LOGSTAT_TAG_COUNT; i++) {
        size_t tlen = strlen(tag_names[i]);
        /* Tags are center-padded to 5 chars: " NET ", "STATE", " CFG " etc. */
        if (strncmp(tp, tag_names[i], tlen) == 0 ||
            (len > 36 + tlen + 1 &&
             strncmp(tp + 1, tag_names[i], tlen) == 0))
            return i;
    }

    return -1;
}

// ============================================================================
// PUBLIC API: ANALYZE
// ============================================================================

/**
 * Analyze bot log file and populate result structure.
 */
int logstat_analyze(const char *path, logstat_result_t *result)
{
    if (!path || !result) return -1;

    memset(result, 0, sizeof(*result));

    /* Initialize tag names */
    for (int i = 0; i < LOGSTAT_TAG_COUNT; i++) {
        result->tags[i].name  = tag_names[i];
        result->tags[i].count = 0;
    }

    /* Get file size */
    struct stat st;
    if (stat(path, &st) != 0) {
        LOG_CMD(LOG_WARN, "logstat: stat failed for %s", path);
        return -1;
    }

    result->file_size = (size_t)st.st_size;

    if (st.st_size == 0) {
        LOG_CMD(LOG_INFO, "logstat: file is empty");
        return 0;
    }

    /* Clamp to LOGSTAT_MAX_SIZE — read tail if file is too large */
    size_t read_size = (size_t)st.st_size;
    long   seek_pos  = 0;

    if (read_size > LOGSTAT_MAX_SIZE) {
        read_size = LOGSTAT_MAX_SIZE;
        seek_pos  = st.st_size - (long)LOGSTAT_MAX_SIZE;
        LOG_CMD(LOG_INFO, "logstat: file too large (%zu bytes), reading last %zu bytes",
                (size_t)st.st_size, read_size);
    }

    /* Allocate buffer */
    char *buf = malloc(read_size + 1);
    if (!buf) {
        LOG_CMD(LOG_ERROR, "logstat: malloc failed");
        return -1;
    }

    /* Read file */
    FILE *fp = fopen(path, "r");
    if (!fp) {
        LOG_CMD(LOG_WARN, "logstat: cannot open %s", path);
        free(buf);
        return -1;
    }

    if (seek_pos > 0)
        fseek(fp, seek_pos, SEEK_SET);

    size_t bytes_read = fread(buf, 1, read_size, fp);
    fclose(fp);

    buf[bytes_read] = '\0';

    /* SSE4.2 newline count */
    result->total_lines = (long)count_newlines_sse42(buf, bytes_read);

    LOG_CMD(LOG_DEBUG, "logstat: %zu bytes read, %ld lines (SSE4.2)",
            bytes_read, result->total_lines);

    /* Scalar pass: level, tag, hour extraction */
    char *p   = buf;
    char *end = buf + bytes_read;

    while (p < end) {
        /* Find end of line */
        char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t line_len = nl ? (size_t)(nl - p) : (size_t)(end - p);

        /*
         * line_len is always > 0 here: the loop condition p < end guarantees
         * that either nl > p or end > p, so both branches produce a positive value.
         */

        /* Extract hour */
        int hour = extract_hour(p, line_len);
        if (hour >= 0)
            result->hour_counts[hour]++;

        /* Extract level */
        int level = extract_level(p, line_len);
        switch (level) {
            case 0: result->level_error++; break;
            case 1: result->level_warn++;  break;
            case 2: result->level_info++;  break;
            case 3: result->level_debug++; break;
            default: result->level_other++; break;
        }

        /* Extract tag */
        int tag = extract_tag(p, line_len);
        if (tag >= 0)
            result->tags[tag].count++;

        p = nl ? nl + 1 : end;
    }

    /* Find busiest hour */
    result->busiest_hour = 0;
    for (int h = 1; h < LOGSTAT_HOURS; h++) {
        if (result->hour_counts[h] > result->hour_counts[result->busiest_hour])
            result->busiest_hour = h;
    }

    free(buf);

    return 0;
}

// ============================================================================
// PUBLIC API: FORMAT
// ============================================================================

/**
 * Format statistics into Telegram-ready string.
 */
int logstat_format(const logstat_result_t *result, char *buf, size_t size)
{
    if (!result || !buf || size == 0) return -1;

    /* Format file size */
    char size_str[32];
    if (result->file_size >= 1024 * 1024)
        snprintf(size_str, sizeof(size_str), "%.1f MB",
                 (double)result->file_size / (1024 * 1024));
    else if (result->file_size >= 1024)
        snprintf(size_str, sizeof(size_str), "%.1f KB",
                 (double)result->file_size / 1024);
    else
        snprintf(size_str, sizeof(size_str), "%zu B", result->file_size);

    /* Header */
    int written = snprintf(buf, size,
        "📊 *Log Statistics*\n\n"
        "```\n"
        "File size : %s\n"
        "Lines     : %ld\n\n"
        "Levels\n"
        "  ERROR   : %ld\n"
        "  WARN    : %ld\n"
        "  INFO    : %ld\n"
        "  DEBUG   : %ld\n\n"
        "Tags\n",
        size_str,
        result->total_lines,
        result->level_error,
        result->level_warn,
        result->level_info,
        result->level_debug
    );

    if (written < 0 || (size_t)written >= size) return -1;
    size_t used = (size_t)written;

    /* Tags — sorted by count, show top 5 */
    /* Simple selection sort for LOGSTAT_TAG_COUNT elements */
    int order[LOGSTAT_TAG_COUNT];
    for (int i = 0; i < LOGSTAT_TAG_COUNT; i++) order[i] = i;

    for (int i = 0; i < LOGSTAT_TAG_COUNT - 1; i++) {
        for (int j = i + 1; j < LOGSTAT_TAG_COUNT; j++) {
            if (result->tags[order[j]].count > result->tags[order[i]].count) {
                int tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }

    int show = (LOGSTAT_TAG_COUNT < 5) ? LOGSTAT_TAG_COUNT : 5;
    for (int i = 0; i < show; i++) {
        int idx = order[i];
        if (result->tags[idx].count == 0) break;

        long pct = result->total_lines > 0
            ? (result->tags[idx].count * 100) / result->total_lines
            : 0;

        int w = snprintf(buf + used, size - used,
            "  %-5s   : %ld (%ld%%)\n",
            result->tags[idx].name,
            result->tags[idx].count,
            pct);

        if (w < 0 || (size_t)w >= size - used) break;
        used += (size_t)w;
    }

    /* Busiest hour */
    int w = snprintf(buf + used, size - used,
        "\nBusiest   : %02ld:00-%02ld:00 (%ld lines)\n"
        "```",
        result->busiest_hour,
        (result->busiest_hour + 1) % 24,
        result->hour_counts[result->busiest_hour]);

    if (w < 0 || (size_t)w >= size - used) return -1;

    return 0;
}