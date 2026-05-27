/**
 * tg-bot - Telegram bot for system administration
 * cli.h - Command-line interface parsing
 * MIT License - Copyright (c) 2026 ironmist45
 */
#ifndef CLI_H
#define CLI_H

// ============================================================================
// CONSTANTS
// ============================================================================

/* Maximum length of a configuration file path (including null terminator) */
#define CLI_CONFIG_PATH_MAX 256

/*
 * Return codes for cli_process():
 *   CLI_OK       — config path parsed, continue startup
 *   CLI_EXIT_OK  — help or version printed, exit with code 0
 *   CLI_EXIT_ERR — parse error, exit with code 1
 */
#define CLI_OK       0
#define CLI_EXIT_OK  1
#define CLI_EXIT_ERR 2

/*
 * Return codes for cli_cmd_parse_config():
 *   CLI_PARSE_OK  — config valid, all checks passed
 *   CLI_PARSE_ERR — config has errors
 */
#define CLI_PARSE_OK  0
#define CLI_PARSE_ERR 1

// ============================================================================
// TYPES
// ============================================================================

/**
 * CLI execution modes
 */
typedef enum {
    CLI_MODE_RUN,           /* Normal bot startup (default)    */
    CLI_MODE_PARSE_CONFIG,  /* --parse: validate config + exit */
} cli_mode_t;

/**
 * Parsed command-line arguments
 */
typedef struct {
    char config_path[CLI_CONFIG_PATH_MAX];  /* Path to configuration file (-c/--config) */
    char parse_path[CLI_CONFIG_PATH_MAX];   /* Path for --parse mode (-p/--parse)        */
    int  show_help;                         /* Flag: display help and exit (-h/--help)   */
    int  show_version;                      /* Flag: display version and exit (-v/--ver) */
    int  do_parse;                          /* Flag: --parse mode requested              */
} cli_args_t;

// ============================================================================
// PUBLIC API
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
 * @param config_path Output buffer for config file path (size >= CLI_CONFIG_PATH_MAX)
 * @return            CLI_OK / CLI_EXIT_OK / CLI_EXIT_ERR
 */
int cli_process(int argc, char *argv[], char *config_path);

/**
 * Validate config file and print all settings to stdout.
 *
 * Loads the config, prints each parameter with status indicator,
 * and performs filesystem checks for configured paths:
 *   - TOKEN, CHAT_ID        — presence
 *   - LOG_FILE parent dir   — writable
 *   - UPLOAD_DIR            — writable (when upload enabled)
 *   - SSH_KEYS_PATH         — readable
 *   - SUDO_PATH, SYSTEMCTL_PATH, JOURNALCTL_PATH, F2B_WRAPPER_PATH — executable
 *
 * Output uses ANSI color codes: green ✓ / red ✗ / yellow ⚠
 * Does not start the bot, does not connect to Telegram, does not write logs.
 *
 * Exit codes: CLI_PARSE_OK (0) on success, CLI_PARSE_ERR (1) on error.
 *
 * @param path  Path to configuration file
 * @return      CLI_PARSE_OK / CLI_PARSE_ERR
 */
int cli_cmd_parse_config(const char *path);

#endif /* CLI_H */
