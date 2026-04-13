#include <stdio.h>
#include <string.h>
#include "version.h"
#include "logger.h"

void print_help() {
    printf("Usage: %s -c <config>\n", APP_NAME);
}

void print_version() {
    printf("%s version %s\n", APP_NAME, APP_VERSION);
    printf("Target: %s\n", TARGET_OS);
}

int main(int argc, char *argv[]) {

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
    }

    // Инициализация логгера
    if (logger_init("/var/log/tg-bot.log") != 0) {
        printf("ERROR: cannot open log file\n");
        return 1;
    }

    log_msg(LOG_INFO, "Starting %s v%s", APP_NAME, APP_VERSION);

    // Заглушка
    log_msg(LOG_INFO, "Bot initialized");

    logger_close();

    return 0;
}
