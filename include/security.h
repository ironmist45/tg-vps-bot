#ifndef SECURITY_H
#define SECURITY_H

#include <stddef.h>

// ===== INIT =====

// инициализация (на будущее: секреты, storage и т.д.)
void security_init(void);

// ===== CONFIG =====

// разрешённый chat_id
void security_set_allowed_chat(long chat_id);

// TTL токена (сек)
void security_set_token_ttl(int ttl);

// ===== ACCESS CONTROL =====

// проверка доступа по chat_id
int security_is_allowed_chat(long chat_id);

// базовая валидация входящего текста
int security_validate_text(const char *text);

// ===== REBOOT TOKENS =====
//
// Stateless токены:
// не хранятся в памяти,
// валидируются по времени + секрету.
//

// генерация токена
int security_generate_reboot_token(long chat_id);

// проверка токена
int security_validate_reboot_token(long chat_id, int token);

#endif // SECURITY_H
