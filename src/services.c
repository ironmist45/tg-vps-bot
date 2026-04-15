#include "services.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>

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

// ===== получение статуса =====

static int get_service_status(const char *service, char *out, size_t size) {

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "systemctl is-active %s.service 2>/dev/null", service);
    
    LOG_SYS(LOG_DEBUG, "exec cmd: %s", cmd);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        LOG_SYS(LOG_ERROR, "popen() failed for %s", service);
        snprintf(out, size, "unknown");
        return -1;
    }

    if (fgets(out, size, fp) == NULL) {
        LOG_SYS(LOG_WARN, "systemctl returned no output for %s", service);
        snprintf(out, size, "unknown");
        pclose(fp);
        return -1;
    }

    pclose(fp);

    // убрать \n
    out[strcspn(out, "\n")] = 0;

    // 🔥 убрать ведущие пробелы (на всякий случай)
    while (*out == ' ') {
        memmove(out, out + 1, strlen(out));
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

    LOG_STATE(LOG_INFO, "services_get_status() called");
    LOG_STATE(LOG_DEBUG, "services count: %d", services_count);
    LOG_STATE(LOG_DEBUG, "collecting services status...");

    if (!buffer || size == 0) {
        LOG_SYS(LOG_ERROR, "Invalid buffer");
        return -1;
    }

    size_t offset = 0;

    // ===== заголовок =====
    int written = snprintf(buffer + offset,
                           size - offset,
                           "*🧩 SERVICES*\n\n");

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

        // 🛑 защита: буфер уже почти заполнен
        if (offset >= size - 1) {
            LOG_SYS(LOG_WARN, "buffer full before writing");
            break;
        }

        int written = snprintf(buffer + offset,
                               size - offset,
                               "%-12s  %s\n",
                               services[i].display,
                               status_fmt);

        if (written < 0) {
            LOG_SYS(LOG_ERROR, "snprintf error (line)");
            break;
        }

        if (offset >= size || (size_t)written >= size - offset) {
            LOG_SYS(LOG_WARN, "services buffer limit reached");
            break;
        }

        offset += (size_t)written;
}
    LOG_STATE(LOG_DEBUG, "services response ready: %zu bytes", offset);

    return 0;
}
