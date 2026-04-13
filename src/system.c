#include "system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===== helpers =====

static int get_hostname(char *buf, size_t size) {
    FILE *f = fopen("/proc/sys/kernel/hostname", "r");
    if (!f)
        return -1;

    if (!fgets(buf, size, f)) {
        fclose(f);
        return -1;
    }

    buf[strcspn(buf, "\n")] = '\0'; // remove newline
    fclose(f);
    return 0;
}

static int get_kernel(char *buf, size_t size) {
    FILE *f = fopen("/proc/version", "r");
    if (!f)
        return -1;

    if (!fgets(buf, size, f)) {
        fclose(f);
        return -1;
    }

    buf[strcspn(buf, "\n")] = '\0';
    fclose(f);
    return 0;
}

static int get_uptime(char *buf, size_t size) {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f)
        return -1;

    double seconds = 0;

    if (fscanf(f, "%lf", &seconds) != 1) {
        fclose(f);
        return -1;
    }

    fclose(f);

    int days = (int)(seconds / 86400);
    int hours = ((int)seconds % 86400) / 3600;
    int minutes = ((int)seconds % 3600) / 60;

    snprintf(buf, size, "%dd %dh %dm", days, hours, minutes);
    return 0;
}

static int get_memory(char *buf, size_t size) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
        return -1;

    long total = 0, free = 0, available = 0;

    char line[256];

    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %ld kB", &total) == 1)
            continue;
        if (sscanf(line, "MemAvailable: %ld kB", &available) == 1)
            continue;
        if (sscanf(line, "MemFree: %ld kB", &free) == 1)
            continue;
    }

    fclose(f);

    if (total == 0)
        return -1;

    long used = total - (available ? available : free);

    snprintf(buf, size,
             "%ld MB / %ld MB",
             used / 1024,
             total / 1024);

    return 0;
}

static int get_loadavg(char *buf, size_t size) {
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f)
        return -1;

    double l1, l5, l15;

    if (fscanf(f, "%lf %lf %lf", &l1, &l5, &l15) != 3) {
        fclose(f);
        return -1;
    }

    fclose(f);

    snprintf(buf, size, "%.2f %.2f %.2f", l1, l5, l15);
    return 0;
}

// ===== public =====

int system_get_status(char *buf, size_t size) {

    char hostname[128];
    char kernel[256];
    char uptime[128];
    char memory[128];
    char load[128];

    // ===== collect data with fallback =====

    if (get_hostname(hostname, sizeof(hostname)) != 0)
        snprintf(hostname, sizeof(hostname), "unknown");

    if (get_kernel(kernel, sizeof(kernel)) != 0)
        snprintf(kernel, sizeof(kernel), "unknown");

    if (get_uptime(uptime, sizeof(uptime)) != 0)
        snprintf(uptime, sizeof(uptime), "unknown");

    if (get_memory(memory, sizeof(memory)) != 0)
        snprintf(memory, sizeof(memory), "unknown");

    if (get_loadavg(load, sizeof(load)) != 0)
        snprintf(load, sizeof(load), "unknown");

    // ===== format (Telegram Markdown) =====

    snprintf(buf, size,
        "*System status:*\n"
        "Host: `%s`\n"
        "Kernel: `%s`\n"
        "Uptime: `%s`\n"
        "Memory: `%s`\n"
        "Load: `%s`\n",
        hostname,
        kernel,
        uptime,
        memory,
        load
    );

    return 0;
}
