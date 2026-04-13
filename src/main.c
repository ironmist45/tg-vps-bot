#include <stdio.h>
#include <string.h>
#include "version.h"

void print_help() {
    printf("Usage: %s -c <config>\n", APP_NAME);
    printf("Options:\n");
    printf("  -c <path>     Config file\n");
    printf("  --help        Show help\n");
    printf("  --version     Show version\n");
}

void print_version() {
    printf("%s version %s\n", APP_NAME, APP_VERSION);
    printf("Target: %s\n", TARGET_OS);
}

int main(int argc, char *argv[]) {

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_help();
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        }
    }

    printf("tg_bot starting...\n");

    return 0;
}
