#include <stdio.h>
#include <string.h>

#include "version.h"
#include "logger.h"
#include "config.h"
#include "cli.h"

int main(int argc, char *argv[]) {

    cli_args_t args;

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

    // ===== проверка -c =====
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

    log_msg(LOG_INFO, "Starting %s v%s", APP_NAME, APP_VERSION);
    log_msg(LOG_INFO, "Using config: %s", args.config_path);

    // ===== загрузка конфига =====
    config_t cfg;

    if (config_load(args.config_path, &cfg) != 0) {
        log_msg(LOG_ERROR, "Failed to load config");
        logger_close();
        return 1;
    }

    // ===== переинициализация логгера (если задан другой путь) =====
    logger_close();

    if (logger_init(cfg.log_file) != 0) {
        printf("ERROR: cannot open log file: %s\n", cfg.log_file);
        return 1;
    }

    log_msg(LOG_INFO, "Logger initialized: %s", cfg.log_file);
    log_msg(LOG_INFO, "Bot initialized successfully");

    // ===== тут будет основной цикл =====

    logger_close();
    return 0;
}
