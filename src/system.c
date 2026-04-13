#include system.h
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

// ===== CPU LOAD =====

static int get_loadavg(double *l1, double *l5, double *l15) {
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return -1;

    if (fscanf(f, "%lf %lf %lf", l1, l5, l15) != 3) {
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

// ===== MEMORY =====

static int get_meminfo(long *total, long *free, long *available) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;

    char key[64];
    long value;
    char unit[16];

    while (fscanf(f, "%63s %ld %15s\n", key, &value, unit) == 3) {

        if (strcmp(key, "MemTotal:") == 0)
            *total = value;
        else if (strcmp(key, "MemFree:") == 0)
            *free = value;
        else if (strcmp(key, "MemAvailable:") == 0)
            *available = value;
    }

    fclose(f);
    return 0;
}

// ===== DISK =====

static int get_disk_usage(const char *path,
                          unsigned long *total,
                          unsigned long *used,
                          unsigned long *free) {

    struct statvfs vfs;

    if (statvfs(path, &vfs) != 0) {
        return -1;
    }

    *total = vfs.f_blocks * vfs.f_frsize;
    *free  = vfs.f_bfree  * vfs.f_frsize;
    *used  = *total - *free;

    return 0;
}

// ===== UPTIME =====

static int get_uptime(long *uptime_sec) {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return -1;

    double up;
    if (fscanf(f, "%lf", &up) != 1) {
        fclose(f);
        return -1;
    }

    fclose(f);

    *uptime_sec = (long)up;
    return 0;
}

// ===== helpers =====

static void format_uptime(long sec, char *buf, size_t size) {
    int days = sec / 86400;
    sec %= 86400;
    int hours = sec / 3600;
    sec %= 3600;
    int mins = sec / 60;

    snprintf(buf, size, "%dd %dh %dm", days, hours, mins);
}

static void format_bytes(unsigned long bytes, char *buf, size_t size) {
    double gb = bytes / (1024.0 * 1024 * 1024);
    snprintf(buf, size, "%.2f GB", gb);
}

// ===== MAIN =====

int system_get_status(char *buffer, size_t size) {

    double l1=0, l5=0, l15=0;
    long mem_total=0, mem_free=0, mem_avail=0;
    unsigned long disk_total=0, disk_used=0, disk_free=0;
    long uptime=0;

    if (get_loadavg(&l1, &l5, &l15) != 0) {
        log_msg(LOG_WARN, "Failed to read loadavg");
    }

    if (get_meminfo(&mem_total, &mem_free, &mem_avail) != 0) {
        log_msg(LOG_WARN, "Failed to read meminfo");
    }

    if (get_disk_usage("/", &disk_total, &disk_used, &disk_free) != 0) {
        log_msg(LOG_WARN, "Failed to read disk usage");
    }

    if (get_uptime(&uptime) != 0) {
        log_msg(LOG_WARN, "Failed to read uptime");
    }

    // ===== форматирование =====

    char uptime_str[64];
    char disk_total_str[64], disk_used_str[64];
    char mem_total_str[64], mem_used_str[64];

    format_uptime(uptime, uptime_str, sizeof(uptime_str));

    format_bytes(disk_total, disk_total_str, sizeof(disk_total_str));
    format_bytes(disk_used, disk_used_str, sizeof(disk_used_str));

    long mem_used = mem_total - mem_avail;

    snprintf(mem_total_str, sizeof(mem_total_str), "%.2f MB", mem_total / 1024.0);
    snprintf(mem_used_str, sizeof(mem_used_str), "%.2f MB", mem_used / 1024.0);

    // ===== итог =====

    snprintf(buffer, size,
        "=== SYSTEM STATUS ===\n"
        "\n"
        "Load avg: %.2f %.2f %.2f\n"
        "Uptime  : %s\n"
        "\n"
        "Memory  : %s / %s\n"
        "Disk    : %s / %s\n",
        l1, l5, l15,
        uptime_str,
        mem_used_str, mem_total_str,
        disk_used_str, disk_total_str
    );

    return 0;
}
