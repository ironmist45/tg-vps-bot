#include "system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>

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

// ===== STATUS =====

static const char *status_emoji(int percent) {
    if (percent < 50) return "🟢";
    if (percent < 80) return "🟡";
    return "🔴";
}

// ===== CPU =====

static int get_cpu(char *buf, size_t size, int *out_percent) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    long user, nice, system, idle, iowait, irq, softirq;

    if (fscanf(f, "cpu %ld %ld %ld %ld %ld %ld %ld",
               &user, &nice, &system, &idle,
               &iowait, &irq, &softirq) != 7) {
        fclose(f);
        return -1;
    }

    fclose(f);

    long total1 = user + nice + system + idle + iowait + irq + softirq;
    long idle1 = idle + iowait;

    usleep(100000);

    f = fopen("/proc/stat", "r");
    if (!f) return -1;

    if (fscanf(f, "cpu %ld %ld %ld %ld %ld %ld %ld",
               &user, &nice, &system, &idle,
               &iowait, &irq, &softirq) != 7) {
        fclose(f);
        return -1;
    }

    fclose(f);

    long total2 = user + nice + system + idle + iowait + irq + softirq;
    long idle2 = idle + iowait;

    long total_diff = total2 - total1;
    long idle_diff = idle2 - idle1;

    if (total_diff <= 0) return -1;

    int usage = (int)((100 * (total_diff - idle_diff)) / total_diff);

    if (usage < 0) usage = 0;
    if (usage > 100) usage = 100;

    if (out_percent) *out_percent = usage;

    snprintf(buf, size, "%s %d%%", status_emoji(usage), usage);

    return 0;
}

// ===== MEMORY =====

static int get_memory(char *buf, size_t size, int *out_percent) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;

    long total = 0, free = 0, buffers = 0, cached = 0;
    char line[256];

    while (fgets(line, sizeof(line), f)) {
        sscanf(line, "MemTotal: %ld kB", &total);
        sscanf(line, "MemFree: %ld kB", &free);
        sscanf(line, "Buffers: %ld kB", &buffers);
        sscanf(line, "Cached: %ld kB", &cached);
    }

    fclose(f);

    if (total == 0) return -1;

    long used = total - free - buffers - cached;
    if (used < 0) used = 0;

    int percent = (int)((used * 100) / total);
    if (out_percent) *out_percent = percent;

    snprintf(buf, size,
             "%s %ld / %ld MB (%d%%)",
             status_emoji(percent),
             used / 1024,
             total / 1024,
             percent);

    return 0;
}

// ===== DISK =====

static int get_disk(char *buf, size_t size, int *out_percent) {
    struct statvfs vfs;

    if (statvfs("/", &vfs) != 0)
        return -1;

    unsigned long total = vfs.f_blocks * vfs.f_frsize;
    unsigned long free = vfs.f_bfree * vfs.f_frsize;

    if (total == 0) return -1;

    unsigned long used = total - free;

    int percent = (int)((used * 100) / total);
    if (out_percent) *out_percent = percent;

    snprintf(buf, size,
             "%s %lu / %lu GB (%d%%)",
             status_emoji(percent),
             used / (1024UL*1024*1024),
             total / (1024UL*1024*1024),
             percent);

    return 0;
}

// ===== LOAD =====

static int get_loadavg(char *buf, size_t size) {
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return -1;

    double l1, l5, l15;

    if (fscanf(f, "%lf %lf %lf", &l1, &l5, &l15) != 3) {
        fclose(f);
        return -1;
    }

    fclose(f);

    int cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores <= 0) cores = 1;

    double ratio = l1 / cores;

    const char *emoji =
        (ratio < 0.7) ? "🟢" :
        (ratio < 1.2) ? "🟡" : "🔴";

    snprintf(buf, size,
             "%s %.2f / %.2f / %.2f (cores:%d)",
             emoji, l1, l5, l15, cores);

    return 0;
}

// ===== HEALTH =====

static const char *health_label(int score) {
    if (score > 80) return "🟢 HEALTHY";
    if (score > 50) return "🟡 WARNING";
    return "🔴 CRITICAL";
}

// ===== public =====

int system_get_status(char *buf, size_t size) {

    char hostname[128] = "unknown";
    char kernel[128]   = "unknown";
    char uptime[128]   = "unknown";

    char cpu[64]    = "unknown";
    char memory[128]= "unknown";
    char disk[128]  = "unknown";
    char load[128]  = "unknown";

    int cpu_p = 0, mem_p = 0, disk_p = 0;

    get_hostname(hostname, sizeof(hostname));
    get_kernel(kernel, sizeof(kernel));
    get_uptime(uptime, sizeof(uptime));

    get_cpu(cpu, sizeof(cpu), &cpu_p);
    get_memory(memory, sizeof(memory), &mem_p);
    get_disk(disk, sizeof(disk), &disk_p);
    get_loadavg(load, sizeof(load));

    // 🔥 smarter health score (CPU вес больше)
    int score = 100 - ((cpu_p * 2 + mem_p + disk_p) / 4);
    if (score < 0) score = 0;

    const char *health = health_label(score);

    snprintf(buf, size,
        "*📊 SYSTEM STATUS*\n\n"
        "🖥 Host     : `%s`\n"
        "⚙️ Kernel   : `%s`\n"
        "⏱ Uptime   : `%s`\n\n"
        "*🧠 RESOURCES*\n\n"
        "🔥 CPU      : `%s`\n"
        "💾 Memory   : `%s`\n"
        "💿 Disk     : `%s`\n"
        "📈 Load     : `%s`\n\n"
        "*🧠 HEALTH*\n"
        "%s (%d%%)",
        hostname,
        kernel,
        uptime,
        cpu,
        memory,
        disk,
        load,
        health,
        score
    );

    return 0;
}
