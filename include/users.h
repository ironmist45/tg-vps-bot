#ifndef USERS_H
#define USERS_H

#include <stddef.h>

// ===== LOW LEVEL =====
// Возвращает список залогиненных пользователей (who)
int users_get_logged(char *buffer, size_t size);

// ===== HIGH LEVEL (используется в commands.c) =====
// Красивый форматированный вывод для Telegram
int users_get(char *buffer, size_t size);

#endif
