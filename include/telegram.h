#ifndef TELEGRAM_H
#define TELEGRAM_H

// init
int telegram_init(const char *token);

// отправка сообщения
int telegram_send_message(long chat_id, const char *text);

// long polling
int telegram_poll();

#endif
