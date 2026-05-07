/* tg-bot — utils.c — General-purpose utility functions. MIT License © 2026 ironmist45 */

#include "utils.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <arpa/inet.h>

// ============================================================================
// STRING TRIMMING
// ============================================================================

/**
 * Trim leading whitespace from string
 *
 * @param s  String to trim
 * @return   Pointer to first non-whitespace character
 */
static char *ltrim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    return s;
}

/**
 * Trim trailing whitespace from string (in-place)
 *
 * @param s  String to trim
 */
static void rtrim(char *s) {
    char *back = s + strlen(s);
    while (back > s && isspace((unsigned char)*(--back))) {
        *back = '\0';
    }
}

/**
 * Trim both leading and trailing whitespace from string
 *
 * @param s  String to trim (modified in-place)
 * @return   Pointer to trimmed string
 */
char *trim(char *s) {
    s = ltrim(s);
    rtrim(s);
    return s;
}

// ============================================================================
// SAFE STRING COPY
// ============================================================================

/**
 * Safely copy string with overflow protection
 *
 * @param dst       Destination buffer
 * @param dst_size  Size of destination buffer
 * @param src       Source string
 * @return          0 on success, -1 if truncated or invalid parameters
 */
int safe_copy(char *dst, size_t dst_size, const char *src) {
    if (!dst || !src || dst_size == 0) return -1;

    size_t len = strnlen(src, dst_size);

    if (len >= dst_size) {
        return -1;  // String would be truncated
    }

    memcpy(dst, src, len);
    dst[len] = '\0';
    return 0;
}

// ============================================================================
// OUTPUT FORMATTING (TELEGRAM MARKDOWN)
// ============================================================================

/**
 * Wrap text in Markdown code block, escaping internal triple-backticks
 *
 * Minimum meaningful buffer: 8 bytes ("```\n\n```\0").
 * Any "```" in src is replaced with "'''" to avoid breaking the block.
 *
 * @param src   Source text
 * @param dst   Destination buffer
 * @param size  Size of destination buffer
 */
void safe_code_block(const char *src, char *dst, size_t size)
{
    if (!src || !dst || size < 8)
        return;

    size_t used = 0;

    // Open code block
    int written = snprintf(dst, size, "```\n");
    if (written < 0 || (size_t)written >= size)
        return;

    used += (size_t)written;

    // Copy content, escaping internal triple backticks.
    // Reserve 5 bytes: '\n' + "```" + '\0'
    for (size_t i = 0; src[i] && used + 5 < size; i++) {

        // Protect against "```" inside the text (would break formatting)
        if (src[i] == '`' && src[i+1] == '`' && src[i+2] == '`') {

            // Replace with triple single quotes — needs 3 bytes
            if (used + 3 + 5 < size) {
                dst[used++] = '\'';
                dst[used++] = '\'';
                dst[used++] = '\'';
            }

            i += 2;  // Skip the next two backticks
            continue;
        }

        dst[used++] = src[i];
    }

    // Ensure newline before closing block
    if (used > 0 && dst[used - 1] != '\n' && used + 1 < size) {
        dst[used++] = '\n';
    }

    // Close code block (3 bytes + null terminator guaranteed by size >= 8 guard)
    if (used + 4 <= size) {
        dst[used++] = '`';
        dst[used++] = '`';
        dst[used++] = '`';
    }

    if (used < size)
        dst[used] = '\0';
}

// ============================================================================
// NUMBER PARSING
// ============================================================================

/**
 * Parse string to long integer
 *
 * @param str  String to parse
 * @param out  Pointer to store result
 * @return     0 on success, -1 on error (invalid format or overflow)
 */
int parse_long(const char *str, long *out) {
    if (!str || !out) return -1;

    char *end;
    errno = 0;
    long val = strtol(str, &end, 10);

    if (*end != '\0' || errno == ERANGE)
        return -1;

    *out = val;
    return 0;
}

/**
 * Parse string to positive integer (> 0)
 *
 * @param str  String to parse
 * @param out  Pointer to store result
 * @return     0 on success, -1 on error (invalid, zero, negative, overflow)
 */
int parse_int(const char *str, int *out) {
    if (!str || !out) return -1;

    char *end;
    errno = 0;
    long val = strtol(str, &end, 10);

    if (*end != '\0' || errno == ERANGE || val <= 0 || val > INT_MAX)
        return -1;

    *out = (int)val;
    return 0;
}

/**
 * Parse string to non-negative integer (>= 0)
 *
 * Unlike parse_int(), accepts 0 as a valid value.
 * Used for TOTP codes and other contexts where 0 is valid input.
 *
 * @param str  String to parse
 * @param out  Pointer to store result
 * @return     0 on success, -1 on error (invalid, negative, overflow)
 */
int parse_int_nonneg(const char *str, int *out) {
    if (!str || !out) return -1;

    char *end;
    errno = 0;
    long val = strtol(str, &end, 10);

    if (*end != '\0' || errno == ERANGE || val < 0 || val > INT_MAX)
        return -1;

    *out = (int)val;
    return 0;
}

// ============================================================================
// ARGUMENT SPLITTING
// ============================================================================

/**
 * Split space-separated string into argument array
 *
 * Modifies input string by inserting null terminators.
 *
 * @param input     Input string (modified in-place)
 * @param argv      Output array of char pointers
 * @param max_args  Maximum number of arguments to extract
 * @return          Number of arguments, or -1 if exceeds max_args
 */
int split_args(char *input, char *argv[], int max_args) {
    int argc = 0;
    char *saveptr = NULL;

    char *token = strtok_r(input, " ", &saveptr);
    while (token) {

        if (argc >= max_args) {
            return -1;  // Too many arguments
        }

        argv[argc++] = token;
        token = strtok_r(NULL, " ", &saveptr);
    }

    return argc;
}

// ============================================================================
// NETWORK UTILITIES
// ============================================================================

/**
 * Validate IPv4 address format
 *
 * @param ip  String to validate
 * @return    1 if valid IPv4 address, 0 otherwise
 */
int is_safe_ip(const char *ip) {
    if (!ip) return 0;

    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip, &(sa.sin_addr)) == 1;
}

// ============================================================================
// TIME UTILITIES
// ============================================================================

/**
 * Calculate elapsed milliseconds between two timestamps
 *
 * @param start  Start time from clock_gettime(CLOCK_MONOTONIC, ...)
 * @param end    End time from clock_gettime(CLOCK_MONOTONIC, ...)
 * @return       Elapsed milliseconds
 */
long elapsed_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000 +
           (end.tv_nsec - start.tv_nsec) / 1000000;
}