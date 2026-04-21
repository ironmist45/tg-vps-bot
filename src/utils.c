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
 *   - URL encoding
 *   - Network utilities (IP validation)
 *   - Command validation
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
char *ltrim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    return s;
}

/**
 * Trim trailing whitespace from string (in-place)
 * 
 * @param s  String to trim (modified in-place)
 */
void rtrim(char *s) {
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
 * Wrap text in Markdown code block with truncation support
 * 
 * @param input   Input text to wrap
 * @param output  Output buffer
 * @param size    Size of output buffer
 */
void format_code_block(const char *input,
                       char *output,
                       size_t size) {

    // Need minimum space for code block markers
    if (!input || !output || size < 16) {
        if (output && size > 0) output[0] = '\0';
        return;
    }

    int truncated = 0;

    // Reserve space for:
    // ```\n        (4 bytes)
    // \n```        (4 bytes)
    // \n[truncated] (12 bytes)
    // Total: ~24 bytes overhead
    size_t max_content = size - 24;

    size_t input_len = strlen(input);

    if (input_len > max_content) {
        truncated = 1;
    }

    char tmp[max_content + 1];

    strncpy(tmp, input, max_content);
    tmp[max_content] = '\0';

    if (truncated) {
        strncat(tmp, "\n[truncated]", max_content - strlen(tmp));
    }

    snprintf(output, size, "```\n%s\n```", tmp);
}

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
// URL ENCODING
// ============================================================================

/**
 * Check if character is unreserved per RFC 3986
 * 
 * @param c  Character to check
 * @return   1 if unreserved, 0 otherwise
 */
static int is_unreserved(char c) {
    return isalnum((unsigned char)c) ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

/**
 * URL-encode string (application/x-www-form-urlencoded)
 * 
 * Space is encoded as '+' (form encoding, not percent-encoding).
 * 
 * @param src       Source string
 * @param dst       Destination buffer
 * @param dst_size  Size of destination buffer
 * @return          0 on success, -1 on overflow
 */
int url_encode(const char *src, char *dst, size_t dst_size) {

    if (!src || !dst || dst_size == 0) return -1;

    size_t i = 0;
    size_t j = 0;

    while (src[i] != '\0') {

        // Need up to 4 bytes for percent encoding (%XX + null)
        if (j + 4 >= dst_size) {
            return -1;
        }

        if (is_unreserved(src[i])) {
            dst[j++] = src[i];
        }
        else if (src[i] == ' ') {
            dst[j++] = '+';
        }
        else {
            snprintf(&dst[j], 4, "%%%02X", (unsigned char)src[i]);
            j += 3;
        }

        i++;
    }

    dst[j] = '\0';
    return 0;
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
// COMMAND VALIDATION
// ============================================================================

/**
 * Generic command validation
 * 
 * Checks that command arguments are non-NULL and non-empty.
 * 
 * @param argv  Argument array (first element is command name)
 * @param resp  Response buffer for error message
 * @param size  Size of response buffer
 * @return      0 on success, -1 on validation failure
 */
int validate_command(char *argv[], char *resp, size_t size) {
    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    }
    return 0;
}
