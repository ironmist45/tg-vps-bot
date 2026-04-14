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

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        snprintf(out, size, "unknown");
        return -1;
    }

    if (fgets(out, size, fp) == NULL) {
        log_msg(LOG_WARN, "systemctl returned no output for %s", service);
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

// ===== строка =====

static void format_line(char *dst, size_t size,
                        const char *name,
                        const char *status) {

    const char *status_fmt = format_status(status);

    // 🔥 чуть более аккуратный вывод
    snprintf(dst, size,
             "%-12s  %s\n",
             name,
             status_fmt);
}

// ===== основной API =====

int services_get_status(char *buffer, size_t size) {

    buffer[0] = '\0';

    strncat(buffer, "*🧩 SERVICES*\n\n",
            size - strlen(buffer) - 1);

    for (int i = 0; i < services_count; i++) {

        char status[64] = {0};
        char line[128] = {0};

        if (get_service_status(services[i].name,
                               status,
                               sizeof(status)) != 0) {
            log_msg(LOG_WARN,
                    "Failed to get status: %s",
                    services[i].name);
        }

        format_line(line, sizeof(line),
                    services[i].display,
                    status);

        if (strlen(buffer) + strlen(line) >= size - 1)
            break;

        // 🔥 безопасный append
        strncat(buffer, line, size - strlen(buffer) - 1);
    }

    return 0;
}
