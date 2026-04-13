#ifndef TELEGRAM_H
#define TELEGRAM_H

#include <stddef.h>

// ===== INIT / SHUTDOWN =====

// инициализация Telegram API
int telegram_init(const char *token);

// корректное завершение (cleanup curl и т.д.)
void telegram_shutdown(void);

// ===== SEND =====

// отправка сообщения (MarkdownV2)
int telegram_send_message(long chat_id, const char *text);

// отправка без Markdown (для логов и raw вывода)
int telegram_send_plain(long chat_id, const char *text);

// ===== POLLING =====

// long polling (основной цикл)
int telegram_poll(void);

#endif // TELEGRAM_H
