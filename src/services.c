#include "services.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>

#define MAX_SERVICES 10

typedef struct {
    const char *name;
    const char *display;
} service_t;

// ===== whitelist сервисов =====

static service_t services[] = {
    {"ssh", "ssh"},
    {"shadowsocks-libev", "shadowsocks"},
    {"mtg", "mtg"},
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
        snprintf(out, size, "unknown");
        pclose(fp);
        return -1;
    }

    pclose(fp);

    // убрать \n
    out[strcspn(out, "\n")] = 0;

    // если systemctl вернул пустоту
    if (out[0] == '\0') {
        snprintf(out, size, "unknown");
    }

    return 0;
}

// ===== форматирование строки =====

static void format_line(char *dst, size_t size,
                        const char *name,
                        const char *status) {

    const char *icon = "⚪";
    const char *label = status;

    if (strcmp(status, "active") == 0) {
        icon = "🟢";
        label = "running";
    }
    else if (strcmp(status, "inactive") == 0) {
        icon = "🟡";
        label = "stopped";
    }
    else if (strcmp(status, "failed") == 0) {
        icon = "🔴";
        label = "failed";
    }
    else if (strcmp(status, "activating") == 0) {
        icon = "🔵";
        label = "starting";
    }
    else if (strcmp(status, "deactivating") == 0) {
        icon = "🟠";
        label = "stopping";
    }
    else if (strcmp(status, "unknown") == 0) {
        icon = "⚫";
        label = "not found";
    }

    snprintf(dst, size, "%s %-12s : %s\n", icon, name, label);
}

// ===== основной API =====

int services_get_status(char *buffer, size_t size) {

    if (!buffer || size == 0)
        return -1;

    buffer[0] = '\0';

    strncat(buffer, "📦 *Services status*\n\n",
            size - strlen(buffer) - 1);

    int added = 0;

    for (int i = 0; i < services_count; i++) {

        char status[64] = {0};
        char line[128] = {0};

        if (get_service_status(services[i].name,
                               status, sizeof(status)) != 0) {
            log_msg(LOG_WARN,
                    "Failed to get status: %s",
                    services[i].name);
        }

        format_line(line, sizeof(line),
                    services[i].display,
                    status);

        if (strlen(buffer) + strlen(line) >= size - 1) {
            break;
        }

        strcat(buffer, line);
        added++;
    }

    // fallback если вдруг ничего не добавили
    if (added == 0) {
        strncat(buffer, "⚠️ No services available\n",
                size - strlen(buffer) - 1);
    }

    return 0;
}
