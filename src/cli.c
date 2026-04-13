#include "cli.h"
#include "version.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

int cli_parse(int argc, char *argv[], cli_args_t *args) {
    memset(args, 0, sizeof(cli_args_t));

    int opt;

    static struct option long_options[] = {
        {"config",  required_argument, 0, 'c'},
        {"help",    no_argument,       0, 'h'},
        {"version", no_argument,       0, 'v'},
        {0, 0, 0, 0}
    };

    opterr = 0;

    while ((opt = getopt_long(argc, argv, "c:hv", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                strncpy(args->config_path, optarg,
                        sizeof(args->config_path) - 1);
                args->config_path[sizeof(args->config_path) - 1] = '\0';
                break;

            case 'h':
                args->show_help = 1;
                break;

            case 'v':
                args->show_version = 1;
                break;

            case '?':
                return -1;
        }
    }

    return 0;
}

// ===== HELP =====

void cli_print_help() {
    printf("%s v%s\n\n", APP_NAME, APP_VERSION);

    printf("Usage:\n");
    printf("  %s [OPTIONS]\n\n", APP_NAME);

    printf("Options:\n");
    printf("  -c, --config <path>   Path to config file\n");
    printf("  -h, --help            Show this help message\n");
    printf("  -v, --version         Show program version\n\n");

    printf("Examples:\n");
    printf("  %s -c /etc/tg-bot/config.conf\n", APP_NAME);
    printf("  %s --config=/etc/tg-bot/config.conf\n", APP_NAME);
}

// ===== VERSION =====

void cli_print_version() {
    printf("%s version %s\n", APP_NAME, APP_VERSION);
    printf("Target: %s\n", TARGET_OS);
    printf("Copyright (c) %s %s\n", APP_YEAR, APP_AUTHOR);
}
