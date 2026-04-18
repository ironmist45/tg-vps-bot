#include "system.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utmp.h>
#include <sys/statvfs.h>

// ===== CPU load =====

static void get_load(double *l1, double *l5, double *l15) {
    double loads[3] = {0};

    if (getloadavg(loads, 3) == -1) {
        *l1 = *l5 = *l15 = 0.0;
        LOG_STATE(LOG_WARN, "getloadavg() failed");
        return;
    }

    *l1  = loads[0];
    *l5  = loads[1];
    *l15 = loads[2];

    LOG_STATE(LOG_DEBUG,
            "cpu load: %.2f %.2f %.2f",
            *l1, *l5, *l15);
}

// ===== memory =====

static void get_memory(int *used_mb, int *total_mb, int *percent) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        LOG_STATE(LOG_ERROR, "Failed to open /proc/meminfo");
        *used_mb = *total_mb = *percent = 0;
        return;
    }

    long mem_total = 0;
    long mem_available = 0;

    char key[64];
    long value;
    char unit[16] = {0};
    char line[128];

    while (fgets(line, sizeof(line), fp)) {

        int n = sscanf(line, "%63s %ld %15s", key, &value, unit);
        if (n < 2)
            continue;

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

    if (mem_total == 0 || mem_available == 0) {
        LOG_STATE(LOG_WARN, "meminfo incomplete");
        *used_mb = *total_mb = *percent = 0;
        return;
    }

    long used = mem_total - mem_available;

    *total_mb = (int)(mem_total / 1024);
    *used_mb  = (int)(used / 1024);
    *percent  = (int)((used * 100) / mem_total);

    LOG_STATE(LOG_DEBUG,
            "memory: %d/%d MB (%d%%)",
            *used_mb, *total_mb, *percent);
}

// ===== disk =====

static void get_disk(int *used_gb, int *total_gb, int *percent) {

    struct statvfs fs;

    if (statvfs("/", &fs) != 0) {
        LOG_STATE(LOG_WARN, "statvfs failed");
        *used_gb = *total_gb = *percent = 0;
        return;
    }

    unsigned long long total =
        (unsigned long long)fs.f_blocks * fs.f_frsize;

    unsigned long long free =
        (unsigned long long)fs.f_bavail * fs.f_frsize;

    unsigned long long used = total - free;

    *total_gb = (int)(total / (1024 * 1024 * 1024));
    *used_gb  = (int)(used  / (1024 * 1024 * 1024));

    if (total > 0) {
        *percent = (int)((used * 100) / total);
    } else {
        *percent = 0;
    }

    LOG_STATE(LOG_DEBUG,
            "disk: %d/%d GB (%d%%)",
            *used_gb, *total_gb, *percent);
}

// ===== uptime =====

static void get_uptime(int *days, int *hours, int *mins) {
    FILE *fp = fopen("/proc/uptime", "r");
    
    if (!fp) {
        LOG_STATE(LOG_WARN, "Failed to open /proc/uptime");
        *days = *hours = *mins = 0;
        return;
    }
    
    double uptime_sec = 0;

    if (fscanf(fp, "%lf", &uptime_sec) != 1) {
        LOG_STATE(LOG_WARN, "Failed to parse /proc/uptime");
        fclose(fp);
        *days = *hours = *mins = 0;
        return;
    }
    
    fclose(fp);

    long total = (long)uptime_sec;

    *days  = total / 86400;
    *hours = (total % 86400) / 3600;
    *mins  = (total % 3600) / 60;

    LOG_STATE(LOG_DEBUG,
            "uptime: %dd %dh %dm",
            *days, *hours, *mins);
}

// ===== users =====

static int get_user_count() {

    struct utmp *entry;
    int count = 0;

    setutent();

    while ((entry = getutent()) != NULL) {
        if (entry->ut_type == USER_PROCESS) {
            count++;
        }
    }

    endutent();

    LOG_STATE(LOG_DEBUG, "users (utmp): count=%d", count);

    return count;
}

// ===== main API =====

int system_get_status(char *buffer, size_t size) {

    LOG_STATE(LOG_DEBUG, "system_get_status() called");
    
    if (!buffer || size == 0) {
        return -1;
    }

    buffer[0] = '\0';

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

   if (written < 0) {
        LOG_STATE(LOG_ERROR, "system_get_status failed");
        return -1;
    }

    if ((size_t)written >= size) {
        LOG_STATE(LOG_WARN, "system_get_status truncated");
    }

    LOG_STATE(LOG_INFO, "system status built successfully (len=%d)",
                    written);

    return 0;
    
}
