#ifndef TELEGRAM_H
#define TELEGRAM_H

#include "config.h"

// init / cleanup
int telegram_init(const config_t *cfg);
void telegram_cleanup();

// polling
int telegram_poll();

// отправка сообщений
int telegram_send_message(const char *text);
int telegram_send_long_message(const char *text);

#endif
