/**
 * tg-bot - Telegram bot for system administration
 * 
 * cli.c - Command-line interface parsing
 * 
 * Handles command-line argument parsing, help text display,
 * and version information output.
 * 
 * Supports:
 *   -c, --config <path>   Specify configuration file path
 *   -h, --help            Show usage information
 *   -v, --version         Show program version
 * 
 * MIT License
 * 
 * Copyright (c) 2026
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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

/**
 * Parse command-line arguments into cli_args_t structure
 * 
 * @param argc  Argument count (from main)
 * @param argv  Argument vector (from main)
 * @param args  Pointer to cli_args_t structure to populate
 * @return      0 on success, -1 on error
 */
int cli_parse(int argc, char *argv[], cli_args_t *args) {

    // Protect against NULL pointer dereference
    if (!args) {
        return -1;
    }

    // Initialize args structure to zero
    memset(args, 0, sizeof(cli_args_t));

    int opt;

    // Long option definitions (GNU getopt_long style)
    static struct option long_options[] = {
        {"config",  required_argument, 0, 'c'},
        {"help",    no_argument,       0, 'h'},
        {"version", no_argument,       0, 'v'},
        {0, 0, 0, 0}  // Terminator
    };

    // Suppress automatic error messages from getopt
    opterr = 0;

    // Parse options
    while ((opt = getopt_long(argc, argv, "c:hv", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                // Safely copy config path (prevents buffer overflow)
                snprintf(args->config_path,
                         sizeof(args->config_path),
                         "%s", optarg);
                break;

            case 'h':
                args->show_help = 1;
                break;

            case 'v':
                args->show_version = 1;
                break;

            case '?':
                // Unknown option encountered
                fprintf(stderr, "Unknown option\n");
                return -1;
        }
    }

    // Check for unexpected positional arguments (e.g., "./bot foo")
    if (optind < argc) {
        fprintf(stderr, "Unexpected arguments\n");
        return -1;
    }

    return 0;
}

/**
 * Process command-line arguments and handle help/version
 * 
 * @param argc        Argument count
 * @param argv        Argument vector
 * @param config_path Output buffer for config file path
 * @return            0 on success, -1 on error (message already printed)
 */
int cli_process(int argc, char *argv[], char *config_path) {
    cli_args_t args;
    
    if (cli_parse(argc, argv, &args) != 0) {
        fprintf(stderr, "Invalid arguments\n");
        return -1;
    }
    
    if (args.show_help) {
        cli_print_help();
        return -1;  // Special: clean exit but not an error
    }
    
    if (args.show_version) {
        cli_print_version();
        return -1;  // Special: clean exit but not an error
    }
    
    if (args.config_path[0] == '\0') {
        fprintf(stderr, "Config not specified\n");
        return -1;
    }
    
    // Copy config path to output buffer
    snprintf(config_path, 256, "%s", args.config_path);
    return 0;
}

// ============================================================================
// HELP OUTPUT
// ============================================================================

/**
 * Display usage information and available options
 */
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

// ============================================================================
// VERSION OUTPUT
// ============================================================================

/**
 * Display program version, build information, and copyright
 */
void cli_print_version(void) {
    printf("%s v%s (%s)\n", APP_NAME, APP_VERSION, APP_CODENAME);
    printf("Commit: %s\n", TG_BUILD_COMMIT);
    printf("Built: %s\n", TG_BUILD_DATE);
    printf("libcurl: %.14s\n", curl_version());
    printf("OpenSSL: %s\n", OpenSSL_version(0));
    printf("c-ares: %s\n", ares_version(NULL));
    printf("cJSON: %d.%d.%d\n", CJSON_VERSION_MAJOR, CJSON_VERSION_MINOR, CJSON_VERSION_PATCH);
    printf("(c) %s %s\n", APP_YEAR, APP_AUTHOR);
}
