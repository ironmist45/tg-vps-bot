#include "logs.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>

#define MAX_OUTPUT 4096

// ===== whitelist =====

static const char *allowed_services[] = {
    "ssh",
    "shadowsocks-libev",
    "mtg",
};

static const int services_count =
    sizeof(allowed_services) / sizeof(allowed_services[0]);

static int is_allowed(const char *name) {
    for (int i = 0; i < services_count; i++) {
        if (strcmp(name, allowed_services[i]) == 0)
            return 1;
    }
    return 0;
}

// ===== основной API =====

int logs_get(const char *service, char *buffer, size_t size) {

    if (!service || service[0] == '\0') {
        snprintf(buffer, size, "Usage: /logs <service>");
        return -1;
    }

    if (!is_allowed(service)) {
        snprintf(buffer, size, "Service not allowed");
        log_msg(LOG_WARN, "Blocked logs access: %s", service);
        return -1;
    }

    char cmd[256];

    snprintf(cmd, sizeof(cmd),
             "journalctl -u %s.service -n 30 --no-pager 2>&1",
             service);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        snprintf(buffer, size, "Failed to read logs");
        return -1;
    }

    buffer[0] = '\0';

    char line[256];

    while (fgets(line, sizeof(line), fp)) {

        if (strlen(buffer) + strlen(line) >= size - 1)
            break;

        strcat(buffer, line);
    }

    pclose(fp);

    if (buffer[0] == '\0') {
        snprintf(buffer, size, "No logs");
    }

    return 0;
}
