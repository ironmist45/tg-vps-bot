#ifndef SYSTEM_H
#define SYSTEM_H

#include <stddef.h>

int system_get_status(char *buf, size_t size);
int system_get_status_mini(char *buf, size_t size);
int system_get_uptime_str(char *buf, size_t size);

#endif
