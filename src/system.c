#include "system.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/statvfs.h>

// ===== CPU load =====

static void get_load(double *l1, double *l5, double *l15) {
    double loads[3] = {0};

    if (getloadavg(loads, 3) == -1) {
        *l1 = *l5 = *l15 = 0.0;
        log_msg(LOG_WARN, "getloadavg() failed");
        return;
    }

    *l1  = loads[0];
    *l5  = loads[1];
    *l15 = loads[2];
}

// ===== memory =====

static void get_memory(int *used_mb, int *total_mb, int *percent) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        log_msg(LOG_ERROR, "Failed to open /proc/meminfo");
        *used_mb = *total_mb = *percent = 0;
        return;
    }

    long mem_total = 0;
    long mem_available = 0;

    char key[64];
    long value;
    char unit[16];

    while (fscanf(fp, "%63s %ld %15s\n", key, &value, unit) == 3) {

        if (strcmp(key, "MemTotal:") == 0) {
            mem_total = value;
        }
        else if (strcmp(key, "MemAvailable:") == 0) {
            mem_available = value;
        }

        if (mem_total && mem_available)
            break;
    }

    fclose(fp);

    if (mem_total == 0) {
        *used_mb = *total_mb = *percent = 0;
        return;
    }

    long used = mem_total - mem_available;

    *total_mb = (int)(mem_total / 1024);
    *used_mb  = (int)(used / 1024);
    *percent  = (int)((used * 100) / mem_total);
}

// ===== disk =====

static void get_disk(int *used_gb, int *total_gb, int *percent) {

    struct statvfs fs;

    if (statvfs("/", &fs) != 0) {
        log_msg(LOG_WARN, "statvfs failed");
        *used_gb = *total_gb = *percent = 0;
        return;
    }

    unsigned long total = (fs.f_blocks * fs.f_frsize);
    unsigned long free  = (fs.f_bfree  * fs.f_frsize);

    unsigned long used = total - free;

    *total_gb = (int)(total / (1024 * 1024 * 1024));
    *used_gb  = (int)(used  / (1024 * 1024 * 1024));

    if (total > 0) {
        *percent = (int)((used * 100) / total);
    } else {
        *percent = 0;
    }
}

// ===== uptime =====

static void get_uptime(int *days, int *hours, int *mins) {
    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp) {
        *days = *hours = *mins = 0;
        return;
    }

    double uptime_sec = 0;

    if (fscanf(fp, "%lf", &uptime_sec) != 1) {
        fclose(fp);
        *days = *hours = *mins = 0;
        return;
    }

    fclose(fp);

    int total = (int)uptime_sec;

    *days  = total / 86400;
    *hours = (total % 86400) / 3600;
    *mins  = (total % 3600) / 60;
}

// ===== users =====

static int get_user_count() {
    FILE *fp = popen("who | wc -l", "r");
    if (!fp) {
        log_msg(LOG_WARN, "popen(who) failed");
        return 0;
    }

    int users = 0;

    if (fscanf(fp, "%d", &users) != 1) {
        users = 0;
    }

    pclose(fp);

    return users;
}

// ===== main API =====

int system_get_status(char *buffer, size_t size) {

    if (!buffer || size == 0) {
        return -1;
    }

    double l1, l5, l15;
    get_load(&l1, &l5, &l15);

    int used_mem, total_mem, mem_pct;
    get_memory(&used_mem, &total_mem, &mem_pct);

    int used_disk, total_disk, disk_pct;
    get_disk(&used_disk, &total_disk, &disk_pct);

    int days, hours, mins;
    get_uptime(&days, &hours, &mins);

    int users = get_user_count();

    int written = snprintf(buffer, size,
        "🖥 *System*\n"
        "⏱ Uptime: `%dd %dh %dm`\n"
        "👥 Users: `%d`\n\n"

        "⚡ *CPU Load*\n"
        "`%.2f / %.2f / %.2f`\n\n"

        "🧠 *Memory*\n"
        "`%d / %d MB (%d%%)`\n\n"

        "💾 *Disk*\n"
        "`%d / %d GB (%d%%)`",
        days, hours, mins,
        users,
        l1, l5, l15,
        used_mem, total_mem, mem_pct,
        used_disk, total_disk, disk_pct
    );

    if (written < 0 || (size_t)written >= size) {
        log_msg(LOG_WARN, "system_get_status truncated");
    }

    return 0;
}
