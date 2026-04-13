#ifndef TELEGRAM_H
#define TELEGRAM_H

#include "config.h"

int telegram_init(const config_t *cfg);
void telegram_cleanup();

int telegram_poll();
int telegram_send_message(const char *text);

#endif
