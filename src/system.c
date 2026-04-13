#include "system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===== helpers =====

static int get_hostname(char *buf, size_t size) {
    FILE *f = fopen("/proc/sys/kernel/hostname", "r");
    if (!f) return -1;

    if (!fgets(buf, size, f)) {
        fclose(f);
        return -1;
    }

    buf[strcspn(buf, "\n")] = '\0';
    fclose(f);
    return 0;
}

// 👉 укороченный kernel
static int get_kernel(char *buf, size_t size) {
    FILE *f = fopen("/proc/version", "r");
    if (!f) return -1;

    char tmp[256];

    if (!fgets(tmp, sizeof(tmp), f)) {
        fclose(f);
        return -1;
    }

    fclose(f);

    char *token = strtok(tmp, " ");
    int count = 0;

    buf[0] = '\0';

    while (token && count < 3) {
        if (count > 0)
            strncat(buf, " ", size - strlen(buf) - 1);

        strncat(buf, token, size - strlen(buf) - 1);

        token = strtok(NULL, " ");
        count++;
    }

    return 0;
}

static int get_uptime(char *buf, size_t size) {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return -1;

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

// ===== 🧠 MEMORY (FIXED + COLORS) =====

static const char *mem_status_emoji(int percent) {
    if (percent < 50) return "🟢";
    if (percent < 80) return "🟡";
    return "🔴";
}

static int get_memory(char *buf, size_t size) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;

    long total = 0;
    long free = 0;
    long buffers = 0;
    long cached = 0;

    char line[256];

    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %ld kB", &total) == 1) continue;
        if (sscanf(line, "MemFree: %ld kB", &free) == 1) continue;
        if (sscanf(line, "Buffers: %ld kB", &buffers) == 1) continue;
        if (sscanf(line, "Cached: %ld kB", &cached) == 1) continue;
    }

    fclose(f);

    if (total == 0) return -1;

    long used = total - free - buffers - cached;
    if (used < 0) used = 0;

    int used_mb = used / 1024;
    int total_mb = total / 1024;

    int percent = (int)((used * 100) / total);

    const char *emoji = mem_status_emoji(percent);

    snprintf(buf, size,
             "%s %d / %d MB (%d%%)",
             emoji,
             used_mb,
             total_mb,
             percent);

    return 0;
}

// ===== 📈 LOAD =====

static const char *load_status_emoji(double load1) {
    if (load1 < 1.0) return "🟢";
    if (load1 < 2.0) return "🟡";
    return "🔴";
}

static int get_loadavg(char *buf, size_t size) {
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return -1;

    double l1, l5, l15;

    if (fscanf(f, "%lf %lf %lf", &l1, &l5, &l15) != 3) {
        fclose(f);
        return -1;
    }

    fclose(f);

    const char *emoji = load_status_emoji(l1);

    snprintf(buf, size,
             "%s %.2f / %.2f / %.2f",
             emoji,
             l1, l5, l15);

    return 0;
}

// ===== public =====

int system_get_status(char *buf, size_t size) {

    char hostname[128];
    char kernel[128];
    char uptime[128];
    char memory[128];
    char load[128];

    // ===== collect =====

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

    // ===== format =====

    snprintf(buf, size,
        "*📊 SYSTEM STATUS*\n\n"
        "🖥 Host     : `%s`\n"
        "⚙️ Kernel   : `%s`\n"
        "⏱ Uptime   : `%s`\n\n"
        "*🧠 RESOURCES*\n\n"
        "💾 Memory   : `%s`\n"
        "📈 Load     : `%s`\n",
        hostname,
        kernel,
        uptime,
        memory,
        load
    );

    return 0;
}
