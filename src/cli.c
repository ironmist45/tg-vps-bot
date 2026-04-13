#include "cli.h"
#include "version.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int cli_parse(int argc, char *argv[], cli_args_t *args) {
    memset(args, 0, sizeof(cli_args_t));

    int opt;

    // getopt только для коротких
    while ((opt = getopt(argc, argv, "c:")) != -1) {
        switch (opt) {
            case 'c':
                strncpy(args->config_path, optarg, sizeof(args->config_path) - 1);
                break;
            default:
                return -1;
        }
    }

    // обработка длинных аргументов вручную
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            args->show_help = 1;
        }
        else if (strcmp(argv[i], "--version") == 0) {
            args->show_version = 1;
        }
    }

    return 0;
}

void cli_print_help() {
    printf("Telegram VPS Bot\n\n");
    printf("Usage:\n");
    printf("  tg_bot -c <config_path>\n\n");

    printf("Options:\n");
    printf("  -c <path>     Path to config file\n");
    printf("  --help        Show this help message\n");
    printf("  --version     Show program version\n\n");

    printf("Example:\n");
    printf("  tg_bot -c /etc/tg-bot/config.conf\n");
}

void cli_print_version() {
    printf("%s version %s\n", APP_NAME, APP_VERSION);
    printf("Target: %s\n", TARGET_OS);
}
