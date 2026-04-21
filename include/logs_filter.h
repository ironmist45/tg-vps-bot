/**
 * tg-bot - Telegram bot for system administration
 * logs_filter.h - Semantic log filtering for /logs command
 * MIT License - Copyright (c) 2026
 */

#ifndef LOGS_FILTER_H
#define LOGS_FILTER_H

// ============================================================================
// CONSTANTS
// ============================================================================

#define MAX_FILTER_WORDS 8  // Maximum number of keywords in multi-filter

// ============================================================================
// TYPES
// ============================================================================

/**
 * Parsed multi-keyword filter
 * 
 * Contains array of keyword pointers (into original string)
 * and count of parsed keywords.
 */
typedef struct {
    char *words[MAX_FILTER_WORDS];  // Array of keyword pointers
    int count;                       // Number of keywords (0 = match all)
} filter_t;

// ============================================================================
// FILTER MATCHING API
// ============================================================================

/**
 * Match a single line against a semantic filter keyword
 * 
 * Supports special keyword groups:
 *   - "error"  : fail, error, invalid, denied, bad, closed
 *   - "auth"   : Accepted, Failed, session
 *   - "brute"  : Failed password, authentication failure, Invalid user, etc.
 *   - "ip"     : lines containing " from " or " rhost="
 *   - "session": session-related messages
 *   - "pam"    : PAM module messages
 * 
 * For other keywords, performs case-insensitive substring search.
 * 
 * @param line    Log line to check
 * @param filter  Filter keyword (semantic group or literal string)
 * @return        1 if line matches, 0 otherwise
 */
int match_semantic(const char *line, const char *filter);

/**
 * Parse space-separated filter string into keyword array
 * 
 * Modifies input string by inserting null terminators.
 * The filter_t structure stores pointers into the modified string.
 * 
 * @param input  Filter string (modified in-place)
 * @param f      Filter structure to populate
 */
void parse_filter(char *input, filter_t *f);

/**
 * Match line against all keywords in filter (AND logic)
 * 
 * A line matches only if it matches EVERY keyword in the filter.
 * Example: "error ssh" matches lines containing both "error" (semantic)
 * and "ssh" (literal).
 * 
 * @param line  Log line to check
 * @param f     Parsed filter structure
 * @return      1 if line matches all keywords, 0 otherwise
 */
int match_filter_multi(const char *line, filter_t *f);

#endif // LOGS_FILTER_H
