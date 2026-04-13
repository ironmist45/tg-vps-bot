#ifndef SECURITY_H
#define SECURITY_H

#include <stddef.h>
#include <stdint.h>

// ===== TYPES =====

// тип токена (явно фиксируем размер)
typedef uint32_t security_token_t;

// ===== INIT =====

// инициализация (логика, секреты, storage)
void security_init(void);

// ===== CONFIG =====

// разрешённый chat_id
void security_set_allowed_chat(long chat_id);

// TTL токена (сек)
void security_set_token_ttl(int ttl);

// ===== ACCESS CONTROL =====

// проверка доступа по chat_id
// ⚠️ в stateless версии может всегда возвращать 1
int security_is_allowed_chat(long chat_id);

// базовая валидация входящего текста
int security_validate_text(const char *text);

// ===== REBOOT TOKENS =====
//
// Stateless токены:
// - не хранятся в памяти
// - валидируются по времени + секрету
// - действуют в пределах TTL
//

// генерация токена
security_token_t security_generate_reboot_token(long chat_id);

// проверка токена (0 = OK, -1 = invalid)
int security_validate_reboot_token(long chat_id, security_token_t token);

#endif // SECURITY_H
