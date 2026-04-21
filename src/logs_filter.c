/**
 * tg-bot - Telegram bot for system administration
 * 
 * logs_filter.c - Semantic log filtering for /logs command
 * 
 * Provides intelligent log filtering beyond simple substring matching.
 * Supports both literal filters and semantic keyword groups:
 *   - error  : matches fail, error, invalid, denied, bad, closed
 *   - auth   : matches Accepted, Failed, session
 *   - brute  : detects brute-force attack patterns
 *   - ip     : lines containing IP addresses
 *   - session: session-related events
 *   - pam    : PAM module messages
 * 
 * Also supports multi-keyword AND filtering (space-separated).
 * All matching is case-insensitive.
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

#define _GNU_SOURCE

#include "logs_filter.h"

#include <string.h>
#include <strings.h>   // for strcasecmp, strcasestr

// ============================================================================
// SEMANTIC FILTER MATCHING
// ============================================================================

/**
 * Match a single line against a semantic filter keyword
 * 
 * Supports special keyword groups that expand to multiple patterns:
 *   - "error"  : matches fail, error, invalid, denied, bad, closed
 *   - "auth"   : matches Accepted, Failed, session
 *   - "brute"  : matches brute-force attack indicators
 *   - "ip"     : matches lines likely containing IP addresses
 *   - "session": matches session-related messages
 *   - "pam"    : matches PAM module messages
 * 
 * For other keywords, performs simple case-insensitive substring search.
 * 
 * @param line    Log line to check
 * @param filter  Filter keyword (semantic group or literal string)
 * @return        1 if line matches, 0 otherwise
 */
int match_semantic(const char *line, const char *filter)
{
    // Empty filter matches everything
    if (!filter || !*filter)
        return 1;

    // ------------------------------------------------------------------------
    // Semantic group: error
    // Matches common error indicators
    // ------------------------------------------------------------------------
    if (strcasecmp(filter, "error") == 0) {
        return strcasestr(line, "fail") != NULL ||
               strcasestr(line, "error") != NULL ||
               strcasestr(line, "invalid") != NULL ||
               strcasestr(line, "denied") != NULL ||
               strcasestr(line, "bad") != NULL ||
               strcasestr(line, "closed") != NULL;
    }

    // ------------------------------------------------------------------------
    // Semantic group: auth
    // Matches authentication-related events
    // ------------------------------------------------------------------------
    if (strcasecmp(filter, "auth") == 0) {
        return strcasestr(line, "Accepted") != NULL ||
               strcasestr(line, "Failed") != NULL ||
               strcasestr(line, "session") != NULL;
    }

    // ------------------------------------------------------------------------
    // Semantic group: brute
    // Detects brute-force attack patterns in logs
    // ------------------------------------------------------------------------
    if (strcasecmp(filter, "brute") == 0) {
        return strcasestr(line, "Failed password") != NULL ||
               strcasestr(line, "authentication failure") != NULL ||
               strcasestr(line, "Invalid user") != NULL ||
               strcasestr(line, "maximum authentication attempts") != NULL ||
               strcasestr(line, "POSSIBLE BREAK-IN ATTEMPT") != NULL;
    }

    // ------------------------------------------------------------------------
    // Semantic group: ip
    // Matches lines that likely contain IP addresses
    // ------------------------------------------------------------------------
    if (strcasecmp(filter, "ip") == 0) {
        return strcasestr(line, " from ") != NULL ||
               strcasestr(line, " rhost=") != NULL;
    }

    // ------------------------------------------------------------------------
    // Semantic group: session
    // Matches session-related log entries
    // ------------------------------------------------------------------------
    if (strcasecmp(filter, "session") == 0) {
        return strcasestr(line, "session") != NULL;
    }

    // ------------------------------------------------------------------------
    // Semantic group: pam
    // Matches PAM (Pluggable Authentication Module) messages
    // ------------------------------------------------------------------------
    if (strcasecmp(filter, "pam") == 0) {
        return strcasestr(line, "pam_") != NULL;
    }

    // ------------------------------------------------------------------------
    // Default: literal substring search (case-insensitive)
    // ------------------------------------------------------------------------
    return strcasestr(line, filter) != NULL;
}

// ============================================================================
// MULTI-KEYWORD FILTER PARSING
// ============================================================================

/**
 * Parse space-separated filter string into keyword array
 * 
 * Modifies input string by inserting null terminators.
 * 
 * @param input  Filter string (modified in-place)
 * @param f      Filter structure to populate
 */
void parse_filter(char *input, filter_t *f) {
    f->count = 0;

    char *tok = strtok(input, " ");
    while (tok && f->count < MAX_FILTER_WORDS) {
        f->words[f->count++] = tok;
        tok = strtok(NULL, " ");
    }
}

// ============================================================================
// MULTI-KEYWORD MATCHING (AND LOGIC)
// ============================================================================

/**
 * Match line against all keywords in filter (AND logic)
 * 
 * A line matches only if it matches EVERY keyword in the filter.
 * For example, "error ssh" matches lines containing both "error" (semantic)
 * and "ssh" (literal).
 * 
 * @param line  Log line to check
 * @param f     Parsed filter structure
 * @return      1 if line matches all keywords, 0 otherwise
 */
int match_filter_multi(const char *line, filter_t *f) {
    // Empty filter matches everything
    if (f->count == 0)
        return 1;

    // All keywords must match (AND logic)
    for (int i = 0; i < f->count; i++) {
        if (!match_semantic(line, f->words[i])) {
            return 0;
        }
    }

    return 1;
}
