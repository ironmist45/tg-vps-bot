#define _GNU_SOURCE

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

    LOG_STATE(LOG_INFO, "logs_get() called with service='%s'",
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

    // 🔒 sanitize filter (strict)
    if (filter) {

        size_t flen = strlen(filter);

        // 📏 ограничение длины
        if (flen > 32) {
            log_msg(LOG_WARN, "filter too long: %s", filter);
            snprintf(buffer, size, "❌ Filter too long");
            return -1;
        }

        // 🔍 проверка символов
        for (size_t i = 0; i < flen; i++) {
            char c = filter[i];

            if (!isalnum((unsigned char)c) &&
                c != '-' &&
                c != '_' &&
                c != '.') {

                log_msg(LOG_WARN,
                        "invalid filter rejected: %s (bad char: %c)",
                        filter, c);

                snprintf(buffer, size, "❌ Invalid filter");
                return -1;
            }
        }

    // ✅ лог успешного фильтра
    log_msg(LOG_DEBUG, "filter accepted: %s", filter);
}

    // ограничение journalctl 200 строк!
    if (lines <= 0)
        lines = 30;

    if (lines > 200) {
        log_msg(LOG_WARN, "lines limit exceeded (%d), clamped to 200", lines);
        lines = 200;
    }

    char cmd[512];

    snprintf(cmd, sizeof(cmd),
        "journalctl -u %s.service -n %d --no-pager 2>&1",
        real_service, lines);

    log_msg(LOG_INFO, "Executing command: %s", cmd);
    log_msg(LOG_DEBUG, "exec cmd: %s", cmd);

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

        // 🔥 early truncation (Telegram-safe)
        if (strlen(buffer) > 3500) {
            safe_append(buffer, size, "\n...\n[truncated]");
            log_msg(LOG_WARN, "logs truncated early (size limit)");
            break;
        }

        sanitize_line(line);

        // 🔥 убрать ANSI / мусор
        line[strcspn(line, "\x1b")] = '\0';

        // 🔍 фильтрация в C (вместо grep)
        if (filter && !strcasestr(line, filter)) {
            continue;
        }

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
    
    log_msg(LOG_INFO,
            "exec done: rc=%d, lines=%d, bytes=%zu",
            rc, line_count, strlen(buffer));

    // ===== fallback если пусто =====

    if (line_count == 0) {

        log_msg(LOG_WARN,
                "No logs for %s, trying global journal", svc);

        snprintf(cmd, sizeof(cmd),
            "journalctl -n %d --no-pager 2>&1",
            lines);

        log_msg(LOG_INFO, "Fallback command: %s", cmd);
        log_msg(LOG_DEBUG, "exec cmd: %s", cmd);

        fp = popen(cmd, "r");
        if (!fp) {
            snprintf(buffer, size,
                "❌ No logs available");
            return -1;
        }

        buffer[0] = '\0';

        snprintf(buffer, size,
            "📜 LOGS: %s (fallback)\n\n",
            svc,
            filter ? " [filtered]" : ""

        line_count = 0;

        while (fgets(line, sizeof(line), fp)) {

            if (strlen(buffer) > 3500) {
                safe_append(buffer, size, "\n...\n[truncated]");
                log_msg(LOG_WARN, "fallback logs truncated early");
                break;
            }

            sanitize_line(line);

            // 🔥 убрать ANSI / мусор
            line[strcspn(line, "\x1b")] = '\0';
            
            if (filter && !strcasestr(line, filter)) {
                continue;
            }

            line_count++;

            if (strlen(buffer) + strlen(line) >= size - 5)
                break;

            safe_append(buffer, size, line);
            safe_append(buffer, size, "\n");
        }

        int rc2 = pclose(fp);

        log_msg(LOG_INFO,
                "fallback exec done: rc=%d, lines=%d, bytes=%zu",
                rc2, line_count, strlen(buffer));
    }

    // ===== если всё ещё пусто =====

    if (line_count == 0) {
        snprintf(buffer, size,
            "📜 LOGS: %s\n\nNo logs found",
            svc);
    }

    log_msg(LOG_DEBUG, "final buffer size: %zu", strlen(buffer));

    return 0;
}
