#include "logs.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

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

// ===== helpers =====

static int is_number(const char *s) {
    if (!s || *s == '\0') return 0;

    for (int i = 0; s[i]; i++) {
        if (!isdigit((unsigned char)s[i]))
            return 0;
    }
    return 1;
}

static void safe_append(char *dst, size_t size, const char *src) {
    size_t len = strlen(dst);
    size_t left = (len < size) ? (size - len - 1) : 0;

    if (left > 0) {
        strncat(dst, src, left);
    }
}

static void sanitize_line(char *line) {
    line[strcspn(line, "\r")] = '\0';
}

// ===== основной API =====

int logs_get(const char *service, char *buffer, size_t size) {

    log_msg(LOG_INFO, "logs_get() called with service='%s'",
            service ? service : "NULL");

    // ===== список сервисов =====

    if (!service || service[0] == '\0') {

        buffer[0] = '\0';

        safe_append(buffer, size, "Available services:\n\n");

        for (int i = 0; i < services_count; i++) {
            safe_append(buffer, size, "- ");
            safe_append(buffer, size, allowed_services[i]);
            safe_append(buffer, size, "\n");
        }

        safe_append(buffer, size, "\nUsage:\n/logs <service> [lines] [filter]");

        return 0;
    }

    // ===== парсинг service аргументов =====
    // service может содержать: "ssh", "ssh 100", "ssh error", "ssh 100 error"

    char svc[64] = {0};
    char arg1[64] = {0};
    char arg2[64] = {0};

    int parsed = sscanf(service, "%63s %63s %63s", svc, arg1, arg2);

    if (!is_allowed(svc)) {
        snprintf(buffer, size, "Service not allowed");
        log_msg(LOG_WARN, "Blocked logs access: %s", svc);
        return -1;
    }

    int lines = 30;
    const char *filter = NULL;

    if (parsed >= 2) {
        if (is_number(arg1)) {
            lines = atoi(arg1);
        } else {
            filter = arg1;
        }
    }

    if (parsed >= 3) {
        filter = arg2;
    }

    // ограничение
    if (lines <= 0 || lines > 1000)
        lines = 30;

    char cmd[512];

    if (filter) {
        snprintf(cmd, sizeof(cmd),
            "journalctl -u %s.service -n %d --no-pager 2>&1 | grep -i '%s'",
            svc, lines, filter);
    } else {
        snprintf(cmd, sizeof(cmd),
            "journalctl -u %s.service -n %d --no-pager 2>&1",
            svc, lines);
    }

    log_msg(LOG_INFO, "Executing command: %s", cmd);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        log_msg(LOG_ERROR, "popen() failed");
        snprintf(buffer, size, "Failed to read logs");
        return -1;
    }

    buffer[0] = '\0';

    // ===== header =====

    snprintf(buffer, size,
        "LOGS: %s (%d lines)%s\n\n",
        svc,
        lines,
        filter ? " [filtered]" : ""
    );

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

    if (line_count == 0) {
        snprintf(buffer, size,
            "LOGS: %s\n\nNo logs available",
            svc);
    }

    log_msg(LOG_INFO, "Final buffer size: %zu", strlen(buffer));

    return 0;
}
