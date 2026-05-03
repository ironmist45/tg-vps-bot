/**
 * tg-bot - Telegram bot for system administration
 * 
 * utils.c - General-purpose utility functions
 * 
 * Provides common helper functions used throughout the codebase:
 *   - String manipulation (trim, safe copy)
 *   - Output formatting (code blocks for Telegram Markdown)
 *   - Number parsing (int, long)
 *   - Argument splitting
 *   - Network utilities (IP validation)
 * 
 * MIT License
 * 
 * Copyright (c) 2026 ironmist45
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

#include "utils.h"

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
 * @param s  String to trim (modified in-place conceptually)
 * @return   Pointer to first non-whitespace character
 */
static char *ltrim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    return s;
}

/**
 * Trim trailing whitespace from string (in-place)
 * 
 * @param s  String to trim (modified in-place)
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
 * Safely wrap text in Markdown code block with triple-backtick escaping
 * 
 * Escapes any "```" sequences in the source text to prevent breaking
 * the code block formatting.
 * 
 * @param src   Source text
 * @param dst   Destination buffer
 * @param size  Size of destination buffer
 */
void safe_code_block(const char *src,
                     char *dst,
                     size_t size)
{
    if (!src || !dst || size == 0)
        return;

    size_t used = 0;

    // Open code block
    int written = snprintf(dst, size, "```\n");
    if (written < 0 || (size_t)written >= size)
        return;

    used += written;

    // Copy content, escaping internal triple backticks
    for (size_t i = 0; src[i] && used < size - 5; i++) {

        // Protect against "```" inside the text (would break formatting)
        if (src[i] == '`' &&
            src[i + 1] &&
            src[i + 2] &&
            src[i + 1] == '`' &&
            src[i + 2] == '`') {

            // Replace with triple single quotes
            if (used < size - 4) {
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
    if (used > 0 && dst[used - 1] != '\n') {
        dst[used++] = '\n';
    }

    // Close code block
    if (used < size - 4) {
        dst[used++] = '`';
        dst[used++] = '`';
        dst[used++] = '`';
    }

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
 * @return     0 on success, -1 on error
 */
int parse_long(const char *str, long *out) {
    if (!str || !out) return -1;

    char *end;
    long val = strtol(str, &end, 10);

    // Entire string must be consumed
    if (*end != '\0') return -1;

    *out = val;
    return 0;
}

/**
 * Parse string to positive integer
 * 
 * @param str  String to parse
 * @param out  Pointer to store result
 * @return     0 on success, -1 on error (invalid, negative, or too large)
 */
int parse_int(const char *str, int *out) {
    if (!str || !out) return -1;

    char *end;
    long val = strtol(str, &end, 10);

    // Must be fully consumed, positive, and fit in int
    if (*end != '\0' || val <= 0 || val > INT_MAX)
        return -1;

    *out = (int)val;
    return 0;
}

/**
 * Parse string to non-negative integer (allows zero)
 *
 * Unlike parse_int(), accepts 0 as a valid value.
 * Used for TOTP codes and other contexts where 0 is valid input.
 *
 * @param str  String to parse
 * @param out  Pointer to store result
 * @return     0 on success, -1 on error (invalid, negative, or too large)
 */
int parse_int_nonneg(const char *str, int *out) {
    if (!str || !out) return -1;
    char *end;
    long val = strtol(str, &end, 10);
    if (*end != '\0' || val < 0 || val > INT_MAX)
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

    char *token = strtok(input, " ");
    while (token) {

        if (argc >= max_args) {
            return -1;  // Too many arguments
        }

        argv[argc++] = token;
        token = strtok(NULL, " ");
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
 * @param start Start time from clock_gettime(CLOCK_MONOTONIC, ...)
 * @param end   End time from clock_gettime(CLOCK_MONOTONIC, ...)
 * @return Elapsed milliseconds
 */
long elapsed_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000 +
           (end.tv_nsec - start.tv_nsec) / 1000000;
}
