#include "services.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

// ===== whitelist сервисов =====

typedef struct {
    const char *name;
    const char *display;
} service_t;

static service_t services[] = {
    {"ssh", "SSH"},
    {"shadowsocks-libev", "Shadowsocks"},
    {"mtg", "MTG"},
};

static const int services_count =
    sizeof(services) / sizeof(services[0]);

static int is_valid_service_name(const char *s) {
    if (!s || *s == '\0') return 0;

    for (size_t i = 0; s[i]; i++) {
        char c = s[i];

        if (!(isalnum((unsigned char)c) ||
              c == '-' || c == '_' )) {
            return 0;
        }
    }

    return 1;
}

// ===== получение статуса =====

static int get_service_status(const char *service, char *out, size_t size) {

    int rc;

    if (!is_valid_service_name(service)) {
        LOG_SYS(LOG_ERROR, "Invalid service name: %s", service);
        if (size > 0) {
            snprintf(out, size, "invalid");
        }
        
        return -1;
    }

    char *const args[] = {
        "sudo",
        "-n",
        "systemctl",
        "is-active",
        (char *)service,
        NULL
    };

    exec_result_t res;

    int rc = exec_command(args, out, size, NULL, &res);

    if (rc != 0) {

        if (res.status == EXEC_TIMEOUT) {
            LOG_SYS(LOG_ERROR, "service %s: timeout", service);
        } else {
            LOG_SYS(LOG_ERROR,
                "service %s: exec failed (%s)",
                service,
                exec_status_str(res.status));
        }

        if (size > 0) {
            snprintf(out, size, "unknown");
        }

        return -1;
    }

    if (res.exit_code != 0) {
        LOG_SYS(LOG_DEBUG,
            "service %s: non-zero exit (%d)",
            service,
            res.exit_code);
    }

    // убрать \n
    out[strcspn(out, "\n")] = 0;

    if (out[0] == '\0') {
        snprintf(out, size, "unknown");
    }

    // 🔥 убрать ведущие пробелы (на всякий случай)
    char *p = out;
    while (*p == ' ') p++;

    if (p != out) {
        memmove(out, p, strlen(p) + 1);
    }

    LOG_SYS(LOG_DEBUG,
            "service=%s status=%s",
            service, out);

    return 0;
}

// ===== emoji + формат =====

static const char *format_status(const char *status) {

    if (strcmp(status, "active") == 0)
        return "🟢 UP";

    if (strcmp(status, "inactive") == 0)
        return "🔴 DOWN";

    if (strcmp(status, "failed") == 0)
        return "🟡 FAIL";

    if (strcmp(status, "activating") == 0)
        return "🟡 STARTING";

    return "⚪ UNKNOWN";
}

// ===== основной API =====

int services_get_status(char *buffer, size_t size) {

    LOG_CMD(LOG_INFO, "services_get_status()");
    LOG_STATE(LOG_DEBUG, "services count: %d", services_count);

    if (!buffer || size == 0) {
        LOG_SYS(LOG_ERROR, "Invalid buffer");
        return -1;
    }

    buffer[0] = '\0';
    size_t offset = 0;

    // ===== заголовок =====
    int written = snprintf(buffer + offset,
                           size - offset,
                           "*🧩 SERVICES*\n\n```\n");

    if (written < 0) {
        LOG_SYS(LOG_ERROR, "snprintf error (header)");
        return -1;
    }

    if ((size_t)written >= size - offset) {
        LOG_SYS(LOG_WARN, "services buffer truncated (header)");
        return 0;
    }

    offset += (size_t)written;

    // ===== сервисы =====
    for (int i = 0; i < services_count; i++) {

        char status[64] = {0};

        if (get_service_status(services[i].name,
                               status,
                               sizeof(status)) != 0) {
            LOG_SYS(LOG_WARN,
                    "Failed to get status: %s",
                    services[i].name);
        }

    const char *status_fmt = format_status(status);

        // 🔍 подробный debug
        LOG_SYS(LOG_DEBUG,
                "service=%s display=%s status=%s",
                services[i].name,
                services[i].display,
                status_fmt);

         int written = snprintf(buffer + offset,
                               size - offset,
                               "%-16s : %s\n",
                               services[i].display,
                               status_fmt);

        if (written < 0) {
            LOG_SYS(LOG_ERROR, "snprintf error (line)");
            break;
        }

        if ((size_t)written >= size - offset) {
            LOG_CMD(LOG_WARN,
                "services buffer limit reached (offset=%zu size=%zu)",
                offset, size);
            break;
        }

        offset += (size_t)written;
}
    // 🔥 ВОТ СЮДА ВСТАВЛЯЕМ FOOTER
    int written_end = snprintf(buffer + offset,
                               size - offset,
                               "\n```");

    if (written_end < 0) {
        LOG_SYS(LOG_ERROR, "snprintf error (footer)");
    } else if ((size_t)written_end >= size - offset) {
        LOG_SYS(LOG_WARN, "services buffer truncated (footer)");
    } else {
        offset += (size_t)written_end;
    }
    
    LOG_STATE(LOG_DEBUG, "services response ready: %zu bytes", offset);

    return 0;
}
