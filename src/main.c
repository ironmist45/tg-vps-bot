#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "version.h"
#include "logger.h"
#include "config.h"
#include "cli.h"
#include "telegram.h"

int main(int argc, char *argv[]) {

    cli_args_t args;

    // ===== CLI =====
    if (cli_parse(argc, argv, &args) != 0) {
        printf("Invalid arguments\n");
        return 1;
    }

    if (args.show_help) {
        cli_print_help();
        return 0;
    }

    if (args.show_version) {
        cli_print_version();
        return 0;
    }

    if (args.config_path[0] == '\0') {
        printf("Error: config file not specified\n");
        printf("Use --help for usage\n");
        return 1;
    }

    // ===== временный логгер (до конфига) =====
    if (logger_init("/var/log/tg-bot.log") != 0) {
        printf("ERROR: cannot open log file\n");
        return 1;
    }

    log_msg(LOG_INFO, "========================================");
    log_msg(LOG_INFO, "Starting %s v%s", APP_NAME, APP_VERSION);
    log_msg(LOG_INFO, "Target: %s", TARGET_OS);
    log_msg(LOG_INFO, "Config: %s", args.config_path);

    // ===== загрузка конфига =====
    config_t cfg;

    if (config_load(args.config_path, &cfg) != 0) {
        log_msg(LOG_ERROR, "Failed to load config");
        logger_close();
        return 1;
    }

    // ===== переинициализация логгера =====
    logger_close();

    if (logger_init(cfg.log_file) != 0) {
        printf("ERROR: cannot open log file: %s\n", cfg.log_file);
        return 1;
    }

    log_msg(LOG_INFO, "Logger initialized: %s", cfg.log_file);
    log_msg(LOG_INFO, "CHAT_ID: %ld", cfg.chat_id);
    log_msg(LOG_INFO, "Polling timeout: %d", cfg.poll_timeout);

    // ===== Telegram init =====
    if (telegram_init(cfg.token) != 0) {
        log_msg(LOG_ERROR, "Failed to init Telegram");
        logger_close();
        return 1;
    }

    log_msg(LOG_INFO, "Telegram bot started");
    log_msg(LOG_INFO, "Entering polling loop");

    // ===== основной цикл =====
    while (1) {

        if (telegram_poll() != 0) {
            log_msg(LOG_WARN, "Polling error, retry in 5 sec");
            sleep(5);
            continue;
        }

        // небольшая пауза (не обязательно, но полезно)
        usleep(200000); // 200 ms
    }

    // (никогда не дойдём сюда, но оставим правильно)
    logger_close();

    return 0;
}
