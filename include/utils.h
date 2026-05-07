/* tg-bot — utils.h — General-purpose utility functions. MIT License © 2026 ironmist45 */

#ifndef UTILS_H
#define UTILS_H

#include "security.h"
#include <stdio.h>
#include <stddef.h>
#include <time.h>   // struct timespec

// ============================================================================
// CONSTANTS
// ============================================================================

#define RESP_MAX 8192  // Maximum response buffer size

// ============================================================================
// STRING TRIMMING
// ============================================================================

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
 * Wrap text in Markdown code block, escaping internal triple-backticks
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
 * @return     0 on success, -1 on error (invalid format, overflow)
 */
int parse_long(const char *str, long *out);

/**
 * Parse string to positive integer (> 0)
 *
 * @param str  String to parse
 * @param out  Pointer to store result
 * @return     0 on success, -1 on error (invalid, zero, negative, overflow)
 */
int parse_int(const char *str, int *out);

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
int parse_int_nonneg(const char *str, int *out);

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
// TIME UTILITIES
// ============================================================================

/**
 * Calculate elapsed milliseconds between two timestamps
 *
 * @param start  Start time from clock_gettime(CLOCK_MONOTONIC, ...)
 * @param end    End time from clock_gettime(CLOCK_MONOTONIC, ...)
 * @return       Elapsed milliseconds
 */
long elapsed_ms(struct timespec start, struct timespec end);

#endif // UTILS_H