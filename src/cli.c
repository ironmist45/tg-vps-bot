/**
 * tg-bot - Telegram bot for system administration
 * cli.c - Command-line interface parsing
 *
 * Supports:
 *   -c, --config <path>   Specify configuration file path
 *   -p, --parse  <path>   Validate config file and print settings
 *   -h, --help            Show usage information
 *   -v, --version         Show program version
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "cli.h"
#include "config.h"
#include "logger.h"
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
        {"parse",   required_argument, 0, 'p'},
        {"help",    no_argument,       0, 'h'},
        {"version", no_argument,       0, 'v'},
        {0, 0, 0, 0}
    };

    opterr = 0;
    int opt;

    while ((opt = getopt_long(argc, argv, "c:p:hv", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                snprintf(args->config_path, sizeof(args->config_path),
                         "%s", optarg);
                break;
            case 'p':
                snprintf(args->parse_path, sizeof(args->parse_path),
                         "%s", optarg);
                args->do_parse = 1;
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
    printf("  -p, --parse  <path>   Validate config file and print settings\n");
    printf("  -h, --help            Show this help message\n");
    printf("  -v, --version         Show program version\n\n");
    printf("Examples:\n");
    printf("  %s -c /etc/tg-bot/config.conf\n", APP_NAME);
    printf("  %s --config=/etc/tg-bot/config.conf\n", APP_NAME);
    printf("  %s -p /etc/tg-bot/config.conf\n", APP_NAME);
    printf("  %s --parse=/etc/tg-bot/config.conf\n", APP_NAME);
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
// PUBLIC API — cli_process
// ============================================================================

/**
 * Process command-line arguments and handle help/version/parse modes.
 *
 * Return codes:
 *   CLI_OK       (0) — config path parsed, continue startup
 *   CLI_EXIT_OK  (1) — help, version or successful --parse, exit 0
 *   CLI_EXIT_ERR (2) — parse error or failed --parse, exit 1
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

    if (args.do_parse) {
        int rc = cli_cmd_parse_config(args.parse_path);
        return (rc == CLI_PARSE_OK) ? CLI_EXIT_OK : CLI_EXIT_ERR;
    }

    if (args.config_path[0] == '\0') {
        fprintf(stderr, "Config not specified\n");
        return CLI_EXIT_ERR;
    }

    snprintf(config_path, CLI_CONFIG_PATH_MAX, "%s", args.config_path);
    return CLI_OK;
}

// ============================================================================
// --parse MODE: config validation and filesystem checks
// ============================================================================

/* ANSI color and style codes */
#define C_GREEN  "\033[32m"
#define C_RED    "\033[31m"
#define C_YELLOW "\033[33m"
#define C_BOLD   "\033[1m"
#define C_RESET  "\033[0m"

/* Section header */
#define SECTION(name) printf("\n" C_BOLD "[%s]" C_RESET "\n", name)

/* Print a plain key=value line with green checkmark */
#define PRINT_VAL(k, v) \
    printf("  " C_GREEN "✓" C_RESET "  %-22s %s\n", k, v)

/*
 * Check if path exists and has the required access mode.
 *
 * warn_only: if 1, access failures produce ⚠ warnings instead of ✗ errors.
 * Use warn_only=1 for paths owned by tg-bot when running as another user
 * (e.g. LOG_FILE dir, UPLOAD_DIR) — the bot itself has the required access.
 *
 * Returns 1 if check passed, 0 otherwise.
 */
static int check_path(const char *label, const char *path,
                      int mode, int warn_only,
                      int *errors, int *warnings)
{
    if (!path || path[0] == '\0') {
        printf("  " C_YELLOW "⚠" C_RESET "  %-22s (not set)\n", label);
        (*warnings)++;
        return 0;
    }

    if (access(path, F_OK) != 0) {
        /* Path does not exist — always an error regardless of warn_only */
        printf("  " C_RED "✗" C_RESET "  %-22s %s — not found\n", label, path);
        (*errors)++;
        return 0;
    }

    if (access(path, mode) != 0) {
        const char *mode_str = (mode == X_OK) ? "not executable"
                             : (mode == W_OK) ? "not writable"
                             :                  "not readable";
        if (warn_only) {
            printf("  " C_YELLOW "⚠" C_RESET "  %-22s %s — %s (run as tg-bot)\n",
                   label, path, mode_str);
            (*warnings)++;
        } else {
            printf("  " C_RED "✗" C_RESET "  %-22s %s — %s\n",
                   label, path, mode_str);
            (*errors)++;
        }
        return 0;
    }

    printf("  " C_GREEN "✓" C_RESET "  %-22s %s\n", label, path);
    return 1;
}

/**
 * Validate config file and print all settings to stdout.
 *
 * Logger is redirected to /dev/null during config_load() to suppress
 * CFG log lines from appearing in --parse terminal output.
 *
 * @param path  Path to configuration file
 * @return      CLI_PARSE_OK (0) on success, CLI_PARSE_ERR (1) on errors
 */
int cli_cmd_parse_config(const char *path)
{
    printf(C_BOLD "%s v%s (%s)" C_RESET " — config parser\n",
           APP_NAME, APP_VERSION, APP_CODENAME);
    printf("File: %s\n", path);

    /*
     * Redirect logger to /dev/null so config_load() LOG_CFG calls
     * don't pollute the --parse terminal output.
     */
    logger_init("/dev/null");

    config_t cfg;
    int load_rc = config_load(path, &cfg);

    logger_close();

    if (load_rc != 0) {
        printf("\n" C_RED C_BOLD "✗ Failed to load config file" C_RESET "\n");
        return CLI_PARSE_ERR;
    }

    int errors   = 0;
    int warnings = 0;

    /* ------------------------------------------------------------------ */
    SECTION("REQUIRED");

    if (cfg.token[0] != '\0') {
        printf("  " C_GREEN "✓" C_RESET "  %-22s set (%zu chars)\n",
               "TOKEN", strlen(cfg.token));
    } else {
        printf("  " C_RED "✗" C_RESET "  %-22s missing\n", "TOKEN");
        errors++;
    }

    if (cfg.chat_id != 0) {
        printf("  " C_GREEN "✓" C_RESET "  %-22s %ld\n", "CHAT_ID", cfg.chat_id);
    } else {
        printf("  " C_RED "✗" C_RESET "  %-22s missing or zero\n", "CHAT_ID");
        errors++;
    }

    /* ------------------------------------------------------------------ */
    SECTION("BOT");

    char ttl_buf[32];
    snprintf(ttl_buf, sizeof(ttl_buf), "%ds", cfg.token_ttl);
    PRINT_VAL("TOKEN_TTL", ttl_buf);
    PRINT_VAL("LOG_FILE",  cfg.log_file);

    /*
     * logger_level_to_string() may return a value with surrounding spaces
     * if the config has them (e.g. "LOG_LEVEL= INFO "). Trim for display.
     */
    {
        const char *lvl_raw = logger_level_to_string(cfg.log_level);
        char lvl_buf[32];
        snprintf(lvl_buf, sizeof(lvl_buf), "%s", lvl_raw);
        /* trim leading spaces */
        char *lvl = lvl_buf;
        while (*lvl == ' ') lvl++;
        /* trim trailing spaces */
        char *end = lvl + strlen(lvl) - 1;
        while (end > lvl && *end == ' ') *end-- = '\0';
        PRINT_VAL("LOG_LEVEL", lvl);
    }

    /*
     * LOG_FILE parent directory — check existence and writability.
     * warn_only=1: directory is owned by root/tg-bot, not by current user.
     * Missing directory is still an error (bot won't start).
     */
    {
        char log_dir[256];
        snprintf(log_dir, sizeof(log_dir), "%s", cfg.log_file);
        char *slash = strrchr(log_dir, '/');
        if (slash && slash != log_dir) {
            *slash = '\0';
            check_path("LOG_FILE dir", log_dir, W_OK, 1, &errors, &warnings);
        }
    }

    /* ------------------------------------------------------------------ */
    SECTION("TOTP");

    if (cfg.totp_secret[0] != '\0') {
        printf("  " C_GREEN "✓" C_RESET "  %-22s set (TOTP 2FA enabled)\n",
               "TOTP_SECRET");
    } else {
        printf("  " C_YELLOW "⚠" C_RESET "  %-22s not set (token flow)\n",
               "TOTP_SECRET");
        warnings++;
    }
    PRINT_VAL("TOTP_SETUP", cfg.totp_setup_enabled ? "enabled" : "disabled");

    /* ------------------------------------------------------------------ */
    SECTION("UPLOAD");

    PRINT_VAL("UPLOAD_ENABLED", cfg.upload_enabled ? "yes" : "no");
    if (cfg.upload_enabled) {
        /*
         * warn_only=1: UPLOAD_DIR is owned by tg-bot.
         * Missing directory is still an error.
         */
        check_path("UPLOAD_DIR", cfg.upload_dir, W_OK, 1, &errors, &warnings);
    } else {
        PRINT_VAL("UPLOAD_DIR",
                  cfg.upload_dir[0] ? cfg.upload_dir : "(not set)");
    }

    /* ------------------------------------------------------------------ */
    SECTION("SSH");

    if (cfg.ssh_keys_path[0] != '\0') {
        /*
         * warn_only=1: authorized_keys is owned by user, not tg-bot.
         * Readable by tg-bot via sudo cat — not by current user directly.
         */
        check_path("SSH_KEYS_PATH", cfg.ssh_keys_path, R_OK, 1, &errors, &warnings);
    } else {
        printf("  " C_YELLOW "⚠" C_RESET "  %-22s not set (/sshkeys disabled)\n",
               "SSH_KEYS_PATH");
        warnings++;
    }

    /* ------------------------------------------------------------------ */
    SECTION("PATHS");

    /* System binaries — must exist and be executable by anyone */
    check_path("SUDO_PATH",        cfg.sudo_path,        X_OK, 0, &errors, &warnings);
    check_path("SYSTEMCTL_PATH",   cfg.systemctl_path,   X_OK, 0, &errors, &warnings);
    check_path("JOURNALCTL_PATH",  cfg.journalctl_path,  X_OK, 0, &errors, &warnings);
    check_path("F2B_WRAPPER_PATH", cfg.f2b_wrapper_path, X_OK, 0, &errors, &warnings);

    /* ------------------------------------------------------------------ */
    printf("\n");

    if (errors == 0 && warnings == 0) {
        printf(C_GREEN C_BOLD "Config OK" C_RESET
               " — 0 errors, 0 warnings\n");
    } else if (errors == 0) {
        printf(C_YELLOW C_BOLD "Config OK" C_RESET
               " — 0 errors, %d warning%s\n",
               warnings, warnings == 1 ? "" : "s");
    } else {
        printf(C_RED C_BOLD "Config FAILED" C_RESET
               " — %d error%s, %d warning%s\n",
               errors,   errors   == 1 ? "" : "s",
               warnings, warnings == 1 ? "" : "s");
    }

    return (errors == 0) ? CLI_PARSE_OK : CLI_PARSE_ERR;
}
