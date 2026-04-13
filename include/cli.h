#ifndef CLI_H
#define CLI_H

typedef struct {
    char config_path[256];
    int show_help;
    int show_version;
} cli_args_t;

int cli_parse(int argc, char *argv[], cli_args_t *args);
void cli_print_help();
void cli_print_version();

#endif
