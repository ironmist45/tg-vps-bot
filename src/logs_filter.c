/**
 * tg-bot - Telegram bot for system administration
 * logs_filter.c - Semantic log filtering for /logs command
 *
 * Supports semantic keyword groups and multi-keyword AND filtering.
 * All matching is case-insensitive.
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#define _GNU_SOURCE

#include "logs_filter.h"

#include <string.h>
#include <strings.h>

// ============================================================================
// SEMANTIC PATTERN TABLES
// ============================================================================

static const char *error_patterns[] = {
    "fail", "error", "invalid", "denied", "bad", "closed", NULL
};

static const char *auth_patterns[] = {
    "Accepted", "Failed", "session", NULL
};

static const char *brute_patterns[] = {
    "Failed password", "authentication failure", "Invalid user",
    "maximum authentication attempts", "POSSIBLE BREAK-IN ATTEMPT", NULL
};

static const char *ip_patterns[] = {
    " from ", " rhost=", NULL
};

static const char *session_patterns[] = {
    "session", NULL
};

static const char *pam_patterns[] = {
    "pam_", NULL
};

// ============================================================================
// SEMANTIC FILTER MATCHING
// ============================================================================

/**
 * Check if line matches any pattern in a NULL-terminated pattern array.
 * All comparisons are case-insensitive.
 */
static int match_any(const char *line, const char **patterns)
{
    for (int i = 0; patterns[i]; i++) {
        if (strcasestr(line, patterns[i]))
            return 1;
    }
    return 0;
}

/**
 * Match a single line against a semantic filter keyword.
 *
 * Supports special keyword groups:
 *   - "error"   : fail, error, invalid, denied, bad, closed
 *   - "auth"    : Accepted, Failed, session
 *   - "brute"   : brute-force attack indicators
 *   - "ip"      : lines likely containing IP addresses
 *   - "session" : session-related messages
 *   - "pam"     : PAM module messages
 *
 * For other keywords: case-insensitive substring search.
 *
 * @param line    Log line to check
 * @param filter  Filter keyword (semantic group or literal string)
 * @return        1 if line matches, 0 otherwise
 */
static int match_semantic(const char *line, const char *filter)
{
    if (!filter || !*filter)
        return 1;

    if (strcasecmp(filter, "error")   == 0) return match_any(line, error_patterns);
    if (strcasecmp(filter, "auth")    == 0) return match_any(line, auth_patterns);
    if (strcasecmp(filter, "brute")   == 0) return match_any(line, brute_patterns);
    if (strcasecmp(filter, "ip")      == 0) return match_any(line, ip_patterns);
    if (strcasecmp(filter, "session") == 0) return match_any(line, session_patterns);
    if (strcasecmp(filter, "pam")     == 0) return match_any(line, pam_patterns);

    return strcasestr(line, filter) != NULL;
}

// ============================================================================
// MULTI-KEYWORD FILTER PARSING
// ============================================================================

/**
 * Parse space-separated filter string into keyword array.
 *
 * Modifies input string by inserting null terminators.
 * Uses strtok_r (thread-safe variant of strtok).
 *
 * @param input  Filter string (modified in-place)
 * @param f      Filter structure to populate
 */
void parse_filter(char *input, filter_t *f) {
    f->count = 0;

    char *saveptr = NULL;
    char *tok = strtok_r(input, " ", &saveptr);
    while (tok && f->count < MAX_FILTER_WORDS) {
        f->words[f->count++] = tok;
        tok = strtok_r(NULL, " ", &saveptr);
    }
}

// ============================================================================
// MULTI-KEYWORD MATCHING (AND LOGIC)
// ============================================================================

/**
 * Match line against all keywords in filter (AND logic).
 *
 * A line matches only if it matches EVERY keyword in the filter.
 * Example: "error ssh" matches lines containing both "error" (semantic)
 * and "ssh" (literal).
 *
 * @param line  Log line to check
 * @param f     Parsed filter structure (const — not modified)
 * @return      1 if line matches all keywords, 0 otherwise
 */
int match_filter_multi(const char *line, const filter_t *f) {
    if (f->count == 0)
        return 1;

    for (int i = 0; i < f->count; i++) {
        if (!match_semantic(line, f->words[i]))
            return 0;
    }

    return 1;
}