#include "logs.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>

#define MAX_LINE 256

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

// ===== простой sanitizer (убираем \r) =====

static void sanitize_line(char *line) {
    line[strcspn(line, "\r")] = '\0';
}

// ===== основной API =====

int logs_get(const char *service, char *buffer, size_t size) {

    if (!service || service[0] == '\0') {
        snprintf(buffer, size,
            "*Usage:*\n`/logs <service>`");
        return -1;
    }

    if (!is_allowed(service)) {
        snprintf(buffer, size,
            "❌ Service *not allowed*");
        log_msg(LOG_WARN, "Blocked logs access: %s", service);
        return -1;
    }

    char cmd[256];

    // 🔒 безопасно (service только из whitelist)
    snprintf(cmd, sizeof(cmd),
             "journalctl -u %s.service -n 30 --no-pager 2>&1",
             service);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        snprintf(buffer, size,
            "❌ Failed to read logs");
        return -1;
    }

    buffer[0] = '\0';

    // ===== header =====

    snprintf(buffer, size,
        "*📜 LOGS: `%s`*\n\n```",
        service);

    char line[MAX_LINE];

    while (fgets(line, sizeof(line), fp)) {

        sanitize_line(line);

        // защита от переполнения + место под ```
        if (strlen(buffer) + strlen(line) >= size - 5)
            break;

        strcat(buffer, line);
    }

    pclose(fp);

    // закрываем code block
    safe_append(buffer, size, "```");

    // если пусто (только header)
    if (strlen(buffer) < 20) {
        snprintf(buffer, size,
            "*📜 LOGS: `%s`*\n\n_No logs available_",
            service);
    }

    return 0;
}
