#define _GNU_SOURCE

#include "logs_filter.h"

#include <string.h>
#include <strings.h>   // 🔥 ВАЖНО: тут лежит strcasestr

int match_semantic(const char *line, const char *filter)
{
    if (!filter || !*filter)
        return 1;

    // error group
    if (strcasecmp(filter, "error") == 0) {
        return strcasestr(line, "fail") != NULL ||
               strcasestr(line, "error") != NULL ||
               strcasestr(line, "invalid") != NULL ||
               strcasestr(line, "denied") != NULL ||
               strcasestr(line, "bad") != NULL ||
               strcasestr(line, "closed") != NULL;
    }

    // 🔥 auth group
    if (strcasecmp(filter, "auth") == 0) {
        return strcasestr(line, "Accepted") != NULL ||
               strcasestr(line, "Failed") != NULL ||
               strcasestr(line, "session") != NULL;
    }

    // 🔥 brute-force detection
    if (strcasecmp(filter, "brute") == 0) {
        return strcasestr(line, "Failed password") != NULL ||
               strcasestr(line, "authentication failure") != NULL ||
               strcasestr(line, "Invalid user") != NULL ||
               strcasestr(line, "maximum authentication attempts") != NULL ||
               strcasestr(line, "POSSIBLE BREAK-IN ATTEMPT") != NULL;
    }

    // 🔥 lines with IP address
    if (strcasecmp(filter, "ip") == 0) {
        return strcasestr(line, " from ") != NULL ||
               strcasestr(line, " rhost=") != NULL;
    }

    // session
    if (strcasecmp(filter, "session") == 0) {
        return strcasestr(line, "session") != NULL;
    }

    // pam
    if (strcasecmp(filter, "pam") == 0) {
        return strcasestr(line, "pam_") != NULL;
    }

    // default
    return strcasestr(line, filter) != NULL;
}

void parse_filter(char *input, filter_t *f) {
    f->count = 0;

    char *tok = strtok(input, " ");
    while (tok && f->count < MAX_FILTER_WORDS) {
        f->words[f->count++] = tok;
        tok = strtok(NULL, " ");
    }
}

int match_filter_multi(const char *line, filter_t *f) {
    if (f->count == 0)
        return 1;

    for (int i = 0; i < f->count; i++) {
        if (!match_semantic(line, f->words[i])) {
            return 0;
        }
        
    }

    return 1;
}
