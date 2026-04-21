/**
 * tg-bot - Telegram bot for system administration
 * cli.h - Command-line interface parsing
 * MIT License - Copyright (c) 2026
 */

#ifndef CLI_H
#define CLI_H

// ============================================================================
// TYPES
// ============================================================================

/**
 * Parsed command-line arguments
 */
typedef struct {
    char config_path[256];  // Path to configuration file (-c/--config)
    int show_help;          // Flag: display help and exit (-h/--help)
    int show_version;       // Flag: display version and exit (-v/--version)
} cli_args_t;

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Parse command-line arguments
 * 
 * Recognized options:
 *   -c, --config <path>   Specify configuration file
 *   -h, --help            Show usage information
 *   -v, --version         Show program version
 * 
 * @param argc  Argument count (from main)
 * @param argv  Argument vector (from main)
 * @param args  Output structure to populate
 * @return      0 on success, -1 on error
 */
int cli_parse(int argc, char *argv[], cli_args_t *args);

/**
 * Process command-line arguments and handle help/version
 * 
 * Validates arguments and handles --help/--version (prints and exits).
 * 
 * @param argc       Argument count
 * @param argv       Argument vector
 * @param config_path Output buffer for config file path (size >= 256)
 * @return           0 on success, -1 on error (already printed)
 */
int cli_process(int argc, char *argv[], char *config_path);

/**
 * Display usage information and available options
 */
void cli_print_help(void);

/**
 * Display program version and copyright information
 */
void cli_print_version(void);

#endif // CLI_H
