#include "logs.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>

#define MAX_LINE 256
#define DEBUG_LINES 5

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

// ===== safe append =====

static void safe_append(char *dst, size_t size, const char *src) {
    size_t len = strlen(dst);
    size_t left = (len < size) ? (size - len - 1) : 0;

    if (left > 0) {
        strncat(dst, src, left);
    }
}

// ===== sanitizer =====

static void sanitize_line(char *line) {
    line[strcspn(line, "\r")] = '\0';
}

// ===== основной API =====

int logs_get(const char *service, char *buffer, size_t size) {

    log_msg(LOG_INFO, "logs_get() called with service='%s'",
            service ? service : "NULL");

    // ===== если /logs без аргументов =====

    if (!service || service[0] == '\0') {

        buffer[0] = '\0';

        safe_append(buffer, size, "Available services:\n\n");

        for (int i = 0; i < services_count; i++) {
            safe_append(buffer, size, "- ");
            safe_append(buffer, size, allowed_services[i]);
            safe_append(buffer, size, "\n");
        }

        safe_append(buffer, size, "\nUsage:\n/logs <service>");

        return 0;
    }

    // ===== проверка whitelist =====

    if (!is_allowed(service)) {
        snprintf(buffer, size,
            "Service not allowed");
        log_msg(LOG_WARN, "Blocked logs access: %s", service);
        return -1;
    }

    char cmd[256];

    snprintf(cmd, sizeof(cmd),
             "journalctl -u %s.service -n 30 --no-pager 2>&1",
             service);

    log_msg(LOG_INFO, "Executing command: %s", cmd);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        log_msg(LOG_ERROR, "popen() failed");
        snprintf(buffer, size,
            "Failed to read logs");
        return -1;
    }

    log_msg(LOG_INFO, "popen() success");

    buffer[0] = '\0';

    // ===== header =====

    snprintf(buffer, size,
        "LOGS: %s\n\n",
        service);

    char line[MAX_LINE];
    int line_count = 0;

    while (fgets(line, sizeof(line), fp)) {

        sanitize_line(line);

        if (line_count < DEBUG_LINES) {
            log_msg(LOG_DEBUG, "LOG LINE[%d]: %s", line_count, line);
        }

        line_count++;

        if (strlen(buffer) + strlen(line) >= size - 5) {
            log_msg(LOG_WARN, "Buffer limit reached");
            break;
        }

        safe_append(buffer, size, line);
        safe_append(buffer, size, "\n");
    }

    int rc = pclose(fp);
    log_msg(LOG_INFO, "pclose() rc=%d, lines=%d", rc, line_count);

    // ===== если нет строк =====

    if (line_count == 0) {
        log_msg(LOG_WARN, "No logs returned from journalctl");

        snprintf(buffer, size,
            "LOGS: %s\n\nNo logs available",
            service);
    }

    log_msg(LOG_INFO, "Final buffer size: %zu", strlen(buffer));

    return 0;
}
