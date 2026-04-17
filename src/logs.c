#define _GNU_SOURCE

#include "logs.h"
#include "logger.h"
#include "exec.h"
#include "utils.h"
#include "logs_filter.h"
#include <strings.h> // для strcasestr
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>


#define MAX_LINE 256
#define DEBUG_LINES 5

// ===== result struct =====

typedef struct {
    int matched;
    int raw;
} logs_result_t;

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

static logs_result_t process_logs_output(char *tmp,
                                        char *buffer,
                                        size_t size,
                                        const char *svc,
                                        int lines,
                                        const char *filter,
                                        int is_fallback)
{

    log_msg(LOG_DEBUG,
        "process_logs_output start: svc=%s filter=%s is_fallback=%d",
        svc,
        filter ? filter : "NULL",
        is_fallback);
    
    buffer[0] = '\0';

    snprintf(buffer, size,
        "📜 LOGS: %s (%s%d lines)%s\n\n",
        svc,
        is_fallback ? "fallback, " : "",
        lines,
        filter ? " [filtered]" : ""
    );

    int dropped = 0;

    char *saveptr;
    char *line = strtok_r(tmp, "\n", &saveptr);
    int raw_lines = 0;

    // 🔥 multi-keyword filter prepare
    filter_t f = {0};

    char filter_copy[128];
    if (filter) {
        strncpy(filter_copy, filter, sizeof(filter_copy));
        filter_copy[sizeof(filter_copy) - 1] = '\0';
        parse_filter(filter_copy, &f);
    }

    while (line) {

        raw_lines++;

        if (raw_lines <= 5) {
            log_msg(LOG_DEBUG, "RAW LINE[%d]: %s", raw_lines, line);
        }

        if (strlen(buffer) > 3500) {
            safe_append(buffer, size,
                "\n...\n⚠ logs truncated (limit reached)");
            log_msg(LOG_WARN,
                "%s logs truncated early",
                is_fallback ? "fallback" : "logs");
            break;
        }

        sanitize_line(line);

        // 🔥 выделение reboot
        if (strstr(line, "-- Reboot --")) {
            safe_append(buffer, size, "\n=== REBOOT ===\n");
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }
        
        // 🔥 СКРЫТЬ "Logs begin at"
        if (strstr(line, "Logs begin at")) {

            log_msg(LOG_DEBUG,
            "Skipping journal header line: %s",
            line);

            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        char *esc = strchr(line, '\x1b');
        if (esc) {
            *esc = '\0';
        }
        
        // 🔥 новый фильтр (multi-keyword, case-insensitive)
        if (filter && !match_filter_multi(line, &f)) {
            dropped++;
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

    log_msg(LOG_DEBUG,
        "process_logs_output done: raw_lines=%d, matched_lines=%d",
        raw_lines, line_count);

    log_msg(LOG_DEBUG,
        "filter stats: dropped=%d matched=%d",
        dropped, line_count);

    if (line_count == 0) {
        log_msg(LOG_DEBUG,
            "No matching lines (filter=%s)",
            filter ? filter : "NULL");
    }
    
    logs_result_t res = {
        .matched = line_count,
        .raw = raw_lines
    
    };

    return res;
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

        // 📏 ограничение длины
        if (flen > 32) {
            log_msg(LOG_WARN, "filter too long: %s", filter);
            snprintf(buffer, size, "❌ Filter too long");
            return -1;
        }

        // ❗ минимальная длина (твоя новая защита)
        if (flen < 3) {
            log_msg(LOG_WARN, "filter too short: %s", filter);
            snprintf(buffer, size, "❌ Filter too short");
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
    log_msg(LOG_DEBUG, "RAW OUTPUT (%s): first 200 chars:\n%.200s", svc, tmp);
    log_msg(LOG_ERROR, "exec_command_simple failed");
    snprintf(buffer, size, "❌ Failed to read logs");
    return -1;
}
 
    logs_result_t res = process_logs_output(
        tmp,
        buffer,
        size,
        svc,
        lines,
        filter,
        0
    );
            
    // ===== дальше сразу fallback если пусто =====

    if (res.raw == 0) {

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

        res = process_logs_output(
            tmp,
            buffer,
            size,
            svc,
            lines,
            filter,
            1
        );

        log_msg(LOG_INFO,
                "fallback exec done: matched=%d raw=%d bytes=%zu",
                res.matched, res.raw, strlen(buffer));
    }

    // ==== Добавляем лог "нет совпадений" ====
    if (res.raw > 0 && res.matched == 0) {
        log_msg(LOG_INFO,
            "No matches for filter='%s' in %s logs",
            filter ? filter : "NULL",
            svc);
    }
    
    // ===== если всё ещё пусто =====

    if (res.matched == 0) {

        if (filter) {
            snprintf(buffer, size,
                "📜 LOGS: %s\n\n"
                "No matches for filter: \"%s\"\n\n"
                "Try:\n"
                "- /logs %s\n"
                "- /logs %s fail\n"
                "- /logs %s Accepted\n",
                svc,
                filter,
                svc, svc, svc);
        } else {
            snprintf(buffer, size,
                "📜 LOGS: %s\n\nNo logs found",
                svc);
        }
    }
    
    log_msg(LOG_DEBUG, "final buffer size: %zu", strlen(buffer));
    
    return 0;
}
