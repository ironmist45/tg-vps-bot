#define _GNU_SOURCE

#include "logs.h"
#include "logger.h"
#include "exec.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>

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

static int process_logs_output(char *tmp,
                               char *buffer,
                               size_t size,
                               const char *svc,
                               int lines,
                               const char *filter,
                               int is_fallback)
{
    buffer[0] = '\0';

    snprintf(buffer, size,
        "📜 LOGS: %s (%s%d lines)%s\n\n",
        svc,
        is_fallback ? "fallback, " : "",
        lines,
        filter ? " [filtered]" : ""
    );

    int line_count = 0;

    char *saveptr;
    char *line = strtok_r(tmp, "\n", &saveptr);

    while (line) {

        if (strlen(buffer) > 3500) {
            safe_append(buffer, size, "\n...\n[truncated]");
            log_msg(LOG_WARN,
                "%s logs truncated early",
                is_fallback ? "fallback" : "logs");
            break;
        }

        sanitize_line(line);
        
        // 🔥 СКРЫТЬ "Logs begin at"
        if (strstr(line, "Logs begin at")) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        line[strcspn(line, "\x1b")] = '\0';

        if (filter && !strcasestr(line, filter)) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        if (line_count < DEBUG_LINES) {
            log_msg(LOG_DEBUG,
                "%s LOG LINE[%d]: %s",
                is_fallback ? "FALLBACK" : "LOG",
                line_count, line);
        }

        line_count++;

        if (strlen(buffer) + strlen(line) >= size - 5) {
            log_msg(LOG_WARN, "Buffer limit reached");
            break;
        }

        safe_append(buffer, size, line);
        safe_append(buffer, size, "\n");

        line = strtok_r(NULL, "\n", &saveptr);
    }

    log_msg(LOG_DEBUG,
        "%s processed: lines=%d, bytes=%zu",
        is_fallback ? "fallback" : "logs",
        line_count, strlen(buffer));

    return line_count;
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
            if (parse_int(arg1, &lines) != 0) {
                snprintf(buffer, size, "❌ Invalid number");
                return -1;
            }
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

        // 🆕 МИНИМАЛЬНАЯ ДЛИНА (защита от CPU abuse)
        if (flen < 3) {
            log_msg(LOG_WARN, "filter too short: %s", filter);
            snprintf(buffer, size, "❌ Filter too short (min 3 chars)");
            return -1;
        }

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

    log_msg(LOG_DEBUG, "filter accepted: %s", filter);
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

   // ===== prepare args for exec =====

    char lines_str[16];
    snprintf(lines_str, sizeof(lines_str), "%d", lines);

    char *const args[] = {
        "sudo",
        "-n",
        "/bin/journalctl",
        "-u",
        (char *)real_service,
        "-n",
        lines_str,
        "--no-pager",
        NULL
    };

// ===== execute =====

char tmp[8192];

if (exec_command_simple(args, tmp, sizeof(tmp)) != 0) {
    log_msg(LOG_ERROR, "exec_command_simple failed");
    snprintf(buffer, size, "❌ Failed to read logs");
    return -1;
}

    int line_count = process_logs_output(
        tmp,
        buffer,
        size,
        svc,
        lines,
        filter,
        0
    );
            
    // ===== дальше сразу fallback если пусто =====

    if (line_count == 0) {

        log_msg(LOG_WARN,
                "No logs for %s, trying global journal", svc);

        char lines_str2[16];
        snprintf(lines_str2, sizeof(lines_str2), "%d", lines);

        char *const fallback_args[] = {
            "sudo",
            "-n",
            "/bin/journalctl",
            "-n",
            lines_str2,
            "--no-pager",
            NULL
        };

        if (exec_command_simple(fallback_args, tmp, sizeof(tmp)) != 0) {
            log_msg(LOG_ERROR, "fallback exec_command_simple failed");
            snprintf(buffer, size, "❌ No logs available");
            return -1;
        }

        line_count = process_logs_output(
            tmp,
            buffer,
            size,
            svc,
            lines,
            filter,
            1
        );
    
        log_msg(LOG_INFO,
                "fallback exec done: lines=%d, bytes=%zu",
                line_count, strlen(buffer));
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
