#ifndef VERSION_H
#define VERSION_H

#include <time.h>

// ===== basic =====
#define APP_NAME    "tg-bot"
#define APP_VERSION "1.2.2"
#define TARGET_OS   "Ubuntu 18.04.6 LTS x86_64"

// ===== meta =====
#define APP_CODENAME "Nebula"
#define APP_YEAR     "2026"
#define APP_AUTHOR   "dedoverde"

// ===== runtime =====
// uptime бота (инициализируется в main.c)
extern time_t g_start_time;

#endif
