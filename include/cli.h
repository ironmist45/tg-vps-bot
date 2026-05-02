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

// ============================================================================
// TYPES
// ============================================================================

/**
 * Parsed command-line arguments
 */
typedef struct {
    char config_path[CLI_CONFIG_PATH_MAX];  // Path to configuration file (-c/--config)
    int show_help;                          // Flag: display help and exit (-h/--help)
    int show_version;                       // Flag: display version and exit (-v/--version)
} cli_args_t;

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Process command-line arguments and handle help/version
 * 
 * Validates arguments and handles --help/--version (prints and exits).
 * 
 * @param argc       Argument count
 * @param argv       Argument vector
 * @param config_path Output buffer for config file path (size >= CLI_CONFIG_PATH_MAX)
 * @return           0 on success, -1 on error (already printed)
 */
int cli_process(int argc, char *argv[], char *config_path);

#endif // CLI_H
