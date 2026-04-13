#include "logs.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define MAX_LINE 256
#define DEBUG_LINES 5

// ===== service mapping (alias → systemd) =====

typedef struct {
    const char *alias;
    const char *systemd;
} service_map_t;

static service_map_t services[] = {
    {"ssh", "ssh"},
    {"shadowsocks", "shadowsocks-libev"},
    {"mtg", "mtg"},
};

static const int services_count =
    sizeof(services) / sizeof(services[0]);

// ===== helpers =====

static int is_number(const char *s) {
    if (!s || *s == '\0') return 0;

    for (int i = 0; s[i]; i++) {
        if (!isdigit((unsigned char)s[i]))
            return 0;
    }
    return 1;
}

static const char* resolve_service(const char *name) {
    for (int i = 0; i < services_count; i++) {
        if (strcmp(name, services[i].alias) == 0)
            return services[i].systemd;
    }
    return NULL;
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

        safe_append(buffer, size, "📜 Available services:\n\n");

        for (int i = 0; i < services_count; i++) {
            safe_append(buffer, size, "- ");
            safe_append(buffer, size, services[i].alias);
            safe_append(buffer, size, "\n");
        }

        safe_append(buffer, size,
            "\nUsage:\n"
            "/logs <service>\n"
            "/logs <service> <lines>\n"
            "/logs <service> <lines> <filter>\n"
        );

        return 0;
    }

    // ===== парсинг аргументов =====

    char svc[64] = {0};
    char arg1[64] = {0};
    char arg2[64] = {0};

    int parsed = sscanf(service, "%63s %63s %63s", svc, arg1, arg2);

    const char *real_service = resolve_service(svc);

    if (!real_service) {
        snprintf(buffer, size, "❌ Service not allowed");
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
            real_service, lines, filter);
    } else {
        snprintf(cmd, sizeof(cmd),
            "journalctl -u %s.service -n %d --no-pager 2>&1",
            real_service, lines);
    }

    log_msg(LOG_INFO, "Executing command: %s", cmd);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        log_msg(LOG_ERROR, "popen() failed");
        snprintf(buffer, size, "❌ Failed to read logs");
        return -1;
    }

    buffer[0] = '\0';

    // ===== header =====

    snprintf(buffer, size,
        "📜 LOGS: %s (%d lines)%s\n\n",
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

    // ===== fallback если пусто =====

    if (line_count == 0) {

        log_msg(LOG_WARN,
                "No logs for %s, trying global journal", svc);

        if (filter) {
            snprintf(cmd, sizeof(cmd),
                "journalctl -n %d --no-pager 2>&1 | grep -i '%s'",
                lines, filter);
        } else {
            snprintf(cmd, sizeof(cmd),
                "journalctl -n %d --no-pager 2>&1 | grep -i '%s'",
                lines, svc);
        }

        log_msg(LOG_INFO, "Fallback command: %s", cmd);

        fp = popen(cmd, "r");
        if (!fp) {
            snprintf(buffer, size,
                "❌ No logs available");
            return -1;
        }

        buffer[0] = '\0';

        snprintf(buffer, size,
            "📜 LOGS: %s (fallback)\n\n",
            svc);

        line_count = 0;

        while (fgets(line, sizeof(line), fp)) {

            sanitize_line(line);
            line_count++;

            if (strlen(buffer) + strlen(line) >= size - 5)
                break;

            safe_append(buffer, size, line);
            safe_append(buffer, size, "\n");
        }

        pclose(fp);
    }

    // ===== если всё ещё пусто =====

    if (line_count == 0) {
        snprintf(buffer, size,
            "📜 LOGS: %s\n\nNo logs found",
            svc);
    }

    log_msg(LOG_INFO, "Final buffer size: %zu", strlen(buffer));

    return 0;
}
