#ifndef EXEC_H
#define EXEC_H

#include <stddef.h>

// основной API
int exec_command(char *const argv[],
                 char *resp,
                 size_t size);

#endif
