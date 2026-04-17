#include <string.h>
#include <strings.h>
#include "logs_filter.h"

int match_semantic(const char *line, const char *filter) {
    if (!filter || !*filter)
        return 1;

    if (strcasecmp(filter, "error") == 0) {
        return strcasestr(line, "fail") ||
               strcasestr(line, "error") ||
               strcasestr(line, "invalid") ||
               strcasestr(line, "denied") ||
               strcasestr(line, "bad") ||
               strcasestr(line, "closed");
    }

    if (strcasecmp(filter, "session") == 0) {
        return strcasestr(line, "session");
    }

    if (strcasecmp(filter, "pam") == 0) {
        return strcasestr(line, "pam_");
    }

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
        if (!strcasestr(line, f->words[i])) {
            return 0;
        }
    }

    return 1;
}
