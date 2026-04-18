#ifndef LOGS_H
#define LOGS_H

#include <stddef.h>

// ===== logs result =====

typedef struct {
    int matched;  // сколько строк прошло фильтр
    int raw;      // сколько строк было всего
} logs_result_t;

// ===== API =====

int logs_get(const char *service, char *buffer, size_t size);

#endif
