/**
 * tg-bot - Telegram bot for system administration
 * utils.h - General-purpose utility functions
 * MIT License - Copyright (c) 2026
 */

#ifndef UTILS_H
#define UTILS_H

#include "security.h"
#include <stdio.h>
#include <stddef.h>

// ============================================================================
// CONSTANTS
// ============================================================================

#define RESP_MAX 8192  // Maximum response buffer size

// ============================================================================
// STRING TRIMMING
// ============================================================================

/**
 * Trim leading whitespace from string
 * 
 * @param s  String to trim (modified in-place conceptually)
 * @return   Pointer to first non-whitespace character
 */
char *ltrim(char *s);

/**
 * Trim trailing whitespace from string (in-place)
 * 
 * @param s  String to trim (modified in-place)
 */
void rtrim(char *s);

/**
 * Trim both leading and trailing whitespace from string
 * 
 * @param s  String to trim (modified in-place)
 * @return   Pointer to trimmed string
 */
char *trim(char *s);

// ============================================================================
// SAFE STRING OPERATIONS
// ============================================================================

/**
 * Safely copy string with overflow protection
 * 
 * @param dst       Destination buffer
 * @param dst_size  Size of destination buffer
 * @param src       Source string
 * @return          0 on success, -1 if truncated or invalid parameters
 */
int safe_copy(char *dst, size_t dst_size, const char *src);

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
void format_code_block(const char *input, char *output, size_t size);

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
void safe_code_block(const char *src, char *dst, size_t size);

// ============================================================================
// NUMBER PARSING
// ============================================================================

/**
 * Parse string to long integer
 * 
 * @param str  String to parse
 * @param out  Pointer to store result
 * @return     0 on success, -1 on error (invalid format or extra characters)
 */
int parse_long(const char *str, long *out);

/**
 * Parse string to positive integer
 * 
 * @param str  String to parse
 * @param out  Pointer to store result
 * @return     0 on success, -1 on error (invalid, negative, zero, or too large)
 */
int parse_int(const char *str, int *out);

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
int split_args(char *input, char *argv[], int max_args);

// ============================================================================
// URL ENCODING
// ============================================================================

/**
 * URL-encode string (application/x-www-form-urlencoded)
 * 
 * Space is encoded as '+' (form encoding, not percent-encoding).
 * Used for Telegram API parameters.
 * 
 * @param src       Source string
 * @param dst       Destination buffer
 * @param dst_size  Size of destination buffer
 * @return          0 on success, -1 on overflow
 */
int url_encode(const char *src, char *dst, size_t dst_size);

// ============================================================================
// NETWORK UTILITIES
// ============================================================================

/**
 * Validate IPv4 address format
 * 
 * @param ip  String to validate
 * @return    1 if valid IPv4 address, 0 otherwise
 */
int is_safe_ip(const char *ip);

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
int validate_command(char *argv[], char *resp, size_t size);

// ============================================================================
// TIME UTILITIES
// ============================================================================

/**
 * Calculate elapsed milliseconds between two timestamps
 * @param start Start time from clock_gettime(CLOCK_MONOTONIC, ...)
 * @param end   End time from clock_gettime(CLOCK_MONOTONIC, ...)
 * @return Elapsed milliseconds
 */
long elapsed_ms(struct timespec start, struct timespec end);

#endif // UTILS_H
