/**
 * tg-bot - Telegram bot for system administration
 * cli.c - Command-line interface parsing
 *
 * Supports:
 *   -c, --config <path>   Specify configuration file path
 *   -h, --help            Show usage information
 *   -v, --version         Show program version
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "cli.h"
#include "version.h"
#include "build_info.h"

#include <curl/curl.h>
#include <openssl/opensslv.h>
#include <openssl/crypto.h>
#include <cjson/cJSON.h>
#include <ares.h>
#include <ares_version.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

// ============================================================================
// COMMAND-LINE PARSING
// ============================================================================

static int cli_parse(int argc, char *argv[], cli_args_t *args) {
    if (!args) return -1;

    memset(args, 0, sizeof(cli_args_t));

    static struct option long_options[] = {
        {"config",  required_argument, 0, 'c'},
        {"help",    no_argument,       0, 'h'},
        {"version", no_argument,       0, 'v'},
        {0, 0, 0, 0}
    };

    opterr = 0;
    int opt;

    while ((opt = getopt_long(argc, argv, "c:hv", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                snprintf(args->config_path, sizeof(args->config_path),
                         "%s", optarg);
                break;
            case 'h':
                args->show_help = 1;
                break;
            case 'v':
                args->show_version = 1;
                break;
            case '?':
                fprintf(stderr, "Unknown option\n");
                return -1;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "Unexpected arguments\n");
        return -1;
    }

    return 0;
}

// ============================================================================
// HELP OUTPUT
// ============================================================================

static void cli_print_help(void) {
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

// ============================================================================
// VERSION OUTPUT
// ============================================================================

static void cli_print_version(void) {
    printf("%s v%s (%s)\n", APP_NAME, APP_VERSION, APP_CODENAME);
    printf("Build: #%s\n", TG_BUILD_NUMBER);
    printf("Commit: %s\n", TG_BUILD_COMMIT);
    printf("Built: %s\n", TG_BUILD_DATE);
    printf("Compiler: %s\n", TG_BUILD_COMPILER);
    printf("libcurl: %.14s\n", curl_version());
    printf("OpenSSL: %s\n", OpenSSL_version(0));
    printf("c-ares: %s\n", ares_version(NULL));
    printf("cJSON: %d.%d.%d\n",
           CJSON_VERSION_MAJOR, CJSON_VERSION_MINOR, CJSON_VERSION_PATCH);
    printf("MIT License (c) %s %s\n", APP_YEAR, APP_AUTHOR);
}

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Process command-line arguments and handle help/version.
 *
 * Return codes:
 *   CLI_OK       (0) — config path parsed, continue startup
 *   CLI_EXIT_OK  (1) — help or version printed, exit with code 0
 *   CLI_EXIT_ERR (2) — parse error, exit with code 1
 *
 * @param argc        Argument count
 * @param argv        Argument vector
 * @param config_path Output buffer for config file path
 * @return            CLI_OK / CLI_EXIT_OK / CLI_EXIT_ERR
 */
int cli_process(int argc, char *argv[], char *config_path) {
    cli_args_t args;

    if (cli_parse(argc, argv, &args) != 0) {
        fprintf(stderr, "Invalid arguments\n");
        return CLI_EXIT_ERR;
    }

    if (args.show_help) {
        cli_print_help();
        return CLI_EXIT_OK;
    }

    if (args.show_version) {
        cli_print_version();
        return CLI_EXIT_OK;
    }

    if (args.config_path[0] == '\0') {
        fprintf(stderr, "Config not specified\n");
        return CLI_EXIT_ERR;
    }

    snprintf(config_path, CLI_CONFIG_PATH_MAX, "%s", args.config_path);
    return CLI_OK;
}
