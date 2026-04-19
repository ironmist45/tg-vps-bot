#ifndef UTILS_H
#define UTILS_H

#include "security.h"
#include <stdio.h>
#include <stddef.h>

#define RESP_MAX 8192

// trim
char *ltrim(char *s);
void rtrim(char *s);
char *trim(char *s);

// безопасные строки
int safe_copy(char *dst, size_t dst_size, const char *src);

// форматирование вывода
void format_code_block(const char *input,
                       char *output,
                       size_t size);

void safe_code_block(const char *src,
                     char *dst,
                     size_t size);

// парсинг
int parse_long(const char *str, long *out);
int parse_int(const char *str, int *out);

// аргументы
int split_args(char *input, char *argv[], int max_args);

// URL encoding (для Telegram)
int url_encode(const char *src, char *dst, size_t dst_size);

// ===== network utils =====
int is_safe_ip(const char *ip);

// ==== общий валидатор команды ====
int validate_command(char *argv[], char *resp, size_t size);

#endif
