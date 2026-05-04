/**
 * tg-bot - Telegram bot for system administration
 * logs_filter.h - Semantic log filtering for /logs command
 * MIT License - Copyright (c) 2026 ironmist45
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
int match_filter_multi(const char *line, const filter_t *f);

#endif // LOGS_FILTER_H
