#include "utils.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

// ===== trim =====

char *ltrim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    return s;
}

void rtrim(char *s) {
    char *back = s + strlen(s);
    while (back > s && isspace((unsigned char)*(--back))) {
        *back = '\0';
    }
}

char *trim(char *s) {
    s = ltrim(s);
    rtrim(s);
    return s;
}

// ===== safe copy =====

int safe_copy(char *dst, size_t dst_size, const char *src) {
    if (!dst || !src || dst_size == 0) return -1;

    size_t len = strlen(src);

    if (len >= dst_size) {
        return -1;
    }

    memcpy(dst, src, len + 1);
    return 0;
}

// ===== реализация форматирования =====

void format_code_block(const char *input,
                       char *output,
                       size_t size) {

    if (!input || !output || size < 16) { // запас под обертку
        if (output && size > 0) output[0] = '\0';
        return;
    }

    int truncated = 0;

    // резервируем место под:
    // ```\n  (4)
    // \n```  (4)
    // \n[truncated] (12)
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

    snprintf(output, size,
             "```\n%s\n```",
             tmp);
}

// ===== parse numbers =====

int parse_long(const char *str, long *out) {
    if (!str || !out) return -1;

    char *end;
    long val = strtol(str, &end, 10);

    if (*end != '\0') return -1;

    *out = val;
    return 0;
}

int parse_int(const char *str, int *out) {
    if (!str || !out) return -1;

    char *end;
    long val = strtol(str, &end, 10);

    if (*end != '\0' || val <= 0) return -1;

    *out = (int)val;
    return 0;
}

// ===== split args =====

int split_args(char *input, char *argv[], int max_args) {
    int argc = 0;

    char *token = strtok(input, " ");
    while (token && argc < max_args) {
        argv[argc++] = token;
        token = strtok(NULL, " ");
    }

    return argc;
}

// ===== URL encode =====

static int is_unreserved(char c) {
    return isalnum((unsigned char)c) ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

int url_encode(const char *src, char *dst, size_t dst_size) {

    if (!src || !dst || dst_size == 0) return -1;

    size_t i = 0;
    size_t j = 0;

    while (src[i] != '\0') {

        if (j + 4 >= dst_size) {
            return -1; // не хватает места
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
