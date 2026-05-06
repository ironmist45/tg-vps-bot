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

// ============================================================================
// TYPES
// ============================================================================

/**
 * Parsed command-line arguments
 */
typedef struct {
    char config_path[CLI_CONFIG_PATH_MAX];  /* Path to configuration file (-c/--config) */
    int show_help;                          /* Flag: display help and exit (-h/--help) */
    int show_version;                       /* Flag: display version and exit (-v/--version) */
} cli_args_t;

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Process command-line arguments and handle help/version.
 *
 * @param argc        Argument count
 * @param argv        Argument vector
 * @param config_path Output buffer for config file path (size >= CLI_CONFIG_PATH_MAX)
 * @return            CLI_OK / CLI_EXIT_OK / CLI_EXIT_ERR
 */
int cli_process(int argc, char *argv[], char *config_path);

#endif // CLI_H