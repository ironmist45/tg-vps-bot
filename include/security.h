#ifndef SECURITY_H
#define SECURITY_H

#include <stddef.h>

// инициализация
void security_init(long allowed_chat_id);

// проверка доступа
int security_is_allowed_chat(long chat_id);

// базовая валидация входящего текста
int security_validate_text(const char *text);

#endif
