#ifndef LOGS_FILTER_H
#define LOGS_FILTER_H

int match_semantic(const char *line, const char *filter);

#define MAX_FILTER_WORDS 8

typedef struct {
    char *words[MAX_FILTER_WORDS];
    int count;
} filter_t;

void parse_filter(char *input, filter_t *f);
int match_filter_multi(const char *line, filter_t *f);

#endif
