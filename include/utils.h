#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

// trim
char *ltrim(char *s);
void rtrim(char *s);
char *trim(char *s);

// безопасные строки
int safe_copy(char *dst, size_t dst_size, const char *src);

// парсинг
int parse_long(const char *str, long *out);
int parse_int(const char *str, int *out);

// аргументы
int split_args(char *input, char *argv[], int max_args);

// URL encoding (для Telegram)
int url_encode(const char *src, char *dst, size_t dst_size);

#endif
