#include "system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <unistd.h>

// ===== helpers =====

static void format_uptime(long seconds, char *buf, size_t size) {
    int days = seconds / 86400;
    int hours = (seconds % 86400) / 3600;
    int mins = (seconds % 3600) / 60;

    snprintf(buf, size, "%dd %dh %dm", days, hours, mins);
}

// ===== main API =====

int system_get_status(char *buffer, size_t size) {

    // ===== CPU LOAD =====
    double load1, load5, load15;
    getloadavg(&load1, &load5, &load15);

    // ===== MEMORY =====
    long total = 0;
    long available = 0;

    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;

    char key[64];
    long value;
    char unit[16];

    while (fscanf(f, "%63s %ld %15s\n", key, &value, unit) == 3) {

        if (strcmp(key, "MemTotal:") == 0)
            total = value;
        else if (strcmp(key, "MemAvailable:") == 0)
            available = value;
    }

    fclose(f);

    total /= 1024;
    available /= 1024;

    long used = total - available;
    int mem_percent = (total > 0) ? (used * 100 / total) : 0;

    // ===== DISK =====
    struct statvfs stat;

    long disk_total = 0;
    long disk_used = 0;
    int disk_percent = 0;

    if (statvfs("/", &stat) == 0) {
        disk_total = (stat.f_blocks * stat.f_frsize) / (1024 * 1024 * 1024);
        disk_used =
            ((stat.f_blocks - stat.f_bfree) * stat.f_frsize) /
            (1024 * 1024 * 1024);

        if (disk_total > 0)
            disk_percent = (disk_used * 100) / disk_total;
    }

    // ===== UPTIME =====
    struct sysinfo info;
    sysinfo(&info);

    char uptime_str[64];
    format_uptime(info.uptime, uptime_str, sizeof(uptime_str));

    // ===== USERS =====
    int users = 0;
    FILE *who = popen("who | wc -l", "r");
    if (who) {
        fscanf(who, "%d", &users);
        pclose(who);
    }

    // ===== FINAL OUTPUT =====

    snprintf(buffer, size,
        "📊 STATUS\n\n"
        "🧠 CPU Load:\n"
        "1m: %.2f | 5m: %.2f | 15m: %.2f\n\n"
        "💾 Memory:\n"
        "%ld / %ld MB (%d%%)\n\n"
        "💽 Disk (/):\n"
        "%ld / %ld GB (%d%%)\n\n"
        "⏱ Uptime:\n"
        "%s\n\n"
        "👥 Users:\n"
        "%d logged in",
        load1, load5, load15,
        used, total, mem_percent,
        disk_used, disk_total, disk_percent,
        uptime_str,
        users
    );

    return 0;
}
