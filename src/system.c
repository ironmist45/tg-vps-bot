#include "system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/statvfs.h>

// ===== helpers =====

static void trim_newline(char *s) {
    if (!s) return;
    s[strcspn(s, "\n")] = 0;
}

// ===== hostname =====

static void get_hostname(char *buf, size_t size) {
    if (gethostname(buf, size) != 0) {
        snprintf(buf, size, "unknown");
    }
}

// ===== kernel =====

static void get_kernel(char *buf, size_t size) {
    FILE *f = fopen("/proc/version", "r");
    if (!f) {
        snprintf(buf, size, "unknown");
        return;
    }

    fgets(buf, size, f);
    trim_newline(buf);
    fclose(f);
}

// ===== uptime =====

static void get_uptime(char *buf, size_t size) {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) {
        snprintf(buf, size, "unknown");
        return;
    }

    double seconds = 0;
    fscanf(f, "%lf", &seconds);
    fclose(f);

    int hours = (int)(seconds / 3600);
    int minutes = (int)((seconds - hours * 3600) / 60);

    snprintf(buf, size, "%dh %dm", hours, minutes);
}

// ===== memory =====

static void get_memory(char *buf, size_t size) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) {
        snprintf(buf, size, "unknown");
        return;
    }

    long total = 0, available = 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        sscanf(line, "MemTotal: %ld kB", &total);
        sscanf(line, "MemAvailable: %ld kB", &available);
    }

    fclose(f);

    long used = total - available;

    snprintf(buf, size, "%ldMB / %ldMB",
             used / 1024, total / 1024);
}

// ===== disk =====

static void get_disk(char *buf, size_t size) {
    struct statvfs stat;

    if (statvfs("/", &stat) != 0) {
        snprintf(buf, size, "unknown");
        return;
    }

    unsigned long total = stat.f_blocks * stat.f_frsize;
    unsigned long free = stat.f_bfree * stat.f_frsize;
    unsigned long used = total - free;

    snprintf(buf, size, "%luGB / %luGB",
             used / (1024 * 1024 * 1024),
             total / (1024 * 1024 * 1024));
}

// ===== IP =====

static void get_ip(char *buf, size_t size) {
    FILE *f = fopen("/proc/net/fib_trie", "r");
    if (!f) {
        snprintf(buf, size, "unknown");
        return;
    }

    char line[256];
    char last_ip[64] = "unknown";

    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "32 host")) {
            if (fgets(line, sizeof(line), f)) {
                sscanf(line, " |-- %63s", last_ip);
            }
        }
    }

    fclose(f);

    snprintf(buf, size, "%s", last_ip);
}

// ===== summary =====

int system_get_summary(char *buf, size_t size) {

    char hostname[128];
    char kernel[256];
    char uptime[64];
    char mem[64];
    char disk[64];
    char ip[64];

    get_hostname(hostname, sizeof(hostname));
    get_kernel(kernel, sizeof(kernel));
    get_uptime(uptime, sizeof(uptime));
    get_memory(mem, sizeof(mem));
    get_disk(disk, sizeof(disk));
    get_ip(ip, sizeof(ip));

    snprintf(buf, size,
        "*tg-bot is running*\n\n"
        "🖥 Host: `%s`\n"
        "🐧 Kernel: `%s`\n"
        "⏱ Uptime: `%s`\n\n"
        "💾 RAM: `%s`\n"
        "📀 Disk: `%s`\n\n"
        "🌐 IP: `%s`\n\n"
        "Use /help to see available commands",
        hostname, kernel, uptime, mem, disk, ip
    );

    return 0;
}

// ===== legacy status (оставим) =====

int system_get_status(char *buf, size_t size) {
    return system_get_summary(buf, size);
}
