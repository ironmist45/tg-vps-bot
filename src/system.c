/**
 * tg-bot - Telegram bot for system administration
 * system.c - System information gathering (/status, /health, /ping)
 *
 * Provides comprehensive system monitoring capabilities:
 *   - CPU load average (1m, 5m, 15m)
 *   - Memory usage (used/total, percentage, visual bar)
 *   - Disk usage (used/total, percentage, visual bar)
 *   - System uptime (from /proc/uptime)
 *   - Bot uptime (from g_start_time)
 *   - Active user count (from utmp)
 *   - OS information (from /etc/os-release)
 *   - Hardware information (from DMI)
 *   - Hostname (from gethostname)
 *
 * Exports two main formats:
 *   - system_get_status()      : Full detailed status with ASCII bars
 *   - system_get_status_mini() : Compact status with emoji indicators
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "system.h"
#include "logger.h"
#include "utils.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utmp.h>
#include <sys/statvfs.h>

/* Bot process start time (defined in lifecycle.c) */
extern time_t g_start_time;

/* Heat indicator thresholds for system_get_status_mini() */
#define SYS_MEM_WARN_PCT   60
#define SYS_MEM_CRIT_PCT   85
#define SYS_DISK_WARN_PCT  70
#define SYS_DISK_CRIT_PCT  90
#define SYS_CPU_WARN_LOAD  1.0
#define SYS_CPU_CRIT_LOAD  2.0

// ============================================================================
// CPU LOAD AVERAGE
// ============================================================================

/**
 * Get system load averages (1, 5, and 15 minutes)
 *
 * @param l1   Output: 1-minute load average
 * @param l5   Output: 5-minute load average
 * @param l15  Output: 15-minute load average
 */
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

    LOG_STATE(LOG_DEBUG, "cpu load: %.2f %.2f %.2f", *l1, *l5, *l15);
}

// ============================================================================
// MEMORY USAGE
// ============================================================================

/**
 * Get memory usage from /proc/meminfo
 *
 * @param used_mb   Output: Used memory in MB
 * @param total_mb  Output: Total memory in MB
 * @param percent   Output: Usage percentage (0-100)
 */
static void get_memory(int *used_mb, int *total_mb, int *percent) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        LOG_STATE(LOG_ERROR, "Failed to open /proc/meminfo");
        *used_mb = *total_mb = *percent = 0;
        return;
    }

    long mem_total     = 0;
    long mem_available = 0;

    char key[64];
    long value;
    char line[128];

    /* /proc/meminfo values are always in kB on Linux — unit field ignored */
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%63s %ld", key, &value) < 2) continue;

        if (strcmp(key, "MemTotal:") == 0)
            mem_total = value;
        else if (strcmp(key, "MemAvailable:") == 0)
            mem_available = value;

        if (mem_total && mem_available) break;
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

    LOG_STATE(LOG_DEBUG, "memory: %d/%d MB (%d%%)", *used_mb, *total_mb, *percent);
}

// ============================================================================
// DISK USAGE
// ============================================================================

/**
 * Get disk usage for root filesystem
 *
 * @param used_gb   Output: Used space in GB
 * @param total_gb  Output: Total space in GB
 * @param percent   Output: Usage percentage (0-100)
 */
static void get_disk(int *used_gb, int *total_gb, int *percent) {
    struct statvfs fs;

    if (statvfs("/", &fs) != 0) {
        LOG_STATE(LOG_WARN, "statvfs failed");
        *used_gb = *total_gb = *percent = 0;
        return;
    }

    unsigned long long total = (unsigned long long)fs.f_blocks * fs.f_frsize;
    unsigned long long free  = (unsigned long long)fs.f_bavail * fs.f_frsize;
    unsigned long long used  = total - free;

    *total_gb = (int)(total / (1024ULL * 1024 * 1024));
    *used_gb  = (int)(used  / (1024ULL * 1024 * 1024));
    *percent  = (total > 0) ? (int)((used * 100) / total) : 0;

    LOG_STATE(LOG_DEBUG, "disk: %d/%d GB (%d%%)", *used_gb, *total_gb, *percent);
}

// ============================================================================
// SYSTEM UPTIME
// ============================================================================

/**
 * Get system uptime from /proc/uptime
 *
 * @param days   Output: Days
 * @param hours  Output: Hours (0-23)
 * @param mins   Output: Minutes (0-59)
 */
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

    LOG_STATE(LOG_DEBUG, "uptime: %dd %dh %dm", *days, *hours, *mins);
}

/**
 * Get bot process uptime as formatted string
 *
 * @param buf   Output buffer
 * @param size  Buffer size
 * @return      0 on success
 */
int system_get_uptime_str(char *buf, size_t size) {
    time_t now = time(NULL);
    int seconds = (int)(now - g_start_time);

    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;

    snprintf(buf, size, "%dh %dm %ds", h, m, s);
    return 0;
}

// ============================================================================
// VISUAL BAR GENERATION
// ============================================================================

/**
 * Generate ASCII progress bar
 *
 * Format: [████████░░░░░░░░░░░░]
 *
 * @param buf      Output buffer
 * @param size     Buffer size
 * @param percent  Fill percentage (0-100)
 */
static void make_bar(char *buf, size_t size, int percent) {
    const int    width         = 20;
    const char   filled_char[] = "█";  /* 3 bytes UTF-8 */
    const char   empty_char[]  = "░";  /* 3 bytes UTF-8 */
    const size_t char_bytes    = 3;

    int filled = (percent * width) / 100;
    int empty  = width - filled;
    int pos    = 0;

    pos += snprintf(buf + pos, size - pos, "[");

    for (int i = 0; i < filled && pos + (int)char_bytes < (int)size; i++) {
        memcpy(buf + pos, filled_char, char_bytes);
        pos += char_bytes;
    }

    for (int i = 0; i < empty && pos + (int)char_bytes < (int)size; i++) {
        memcpy(buf + pos, empty_char, char_bytes);
        pos += char_bytes;
    }

    if (pos < (int)size - 1) {
        buf[pos++] = ']';
        buf[pos]   = '\0';
    }
}

// ============================================================================
// ACTIVE USER COUNT
// ============================================================================

/**
 * Get number of currently logged-in users (from utmp)
 *
 * @return  Number of active user processes
 */
static int get_user_count(void) {
    const struct utmp *entry;
    int count = 0;

    setutent();

    while ((entry = getutent()) != NULL) {
        if (entry->ut_type == USER_PROCESS)
            count++;
    }

    endutent();

    LOG_STATE(LOG_DEBUG, "users (utmp): count=%d", count);
    return count;
}

// ============================================================================
// OPERATING SYSTEM INFORMATION
// ============================================================================

/**
 * Get OS pretty name from /etc/os-release
 *
 * @param buf   Output buffer
 * @param size  Buffer size
 */
static void get_os(char *buf, size_t size) {
    FILE *fp = fopen("/etc/os-release", "r");
    if (!fp) {
        safe_copy(buf, size, "Unknown");
        return;
    }

    char line[256];

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
            char *val = line + 12;

            val[strcspn(val, "\n")] = '\0';

            if (*val == '"' || *val == '\'') {
                val++;
                char *end = strrchr(val, '"');
                if (end) *end = '\0';
            }

            safe_copy(buf, size, val);
            fclose(fp);
            return;
        }
    }

    fclose(fp);
    safe_copy(buf, size, "Unknown");
}

// ============================================================================
// HOSTNAME
// ============================================================================

/**
 * Get system hostname via gethostname()
 *
 * @param buf   Output buffer
 * @param size  Buffer size
 */
static void get_hostname(char *buf, size_t size) {
    if (gethostname(buf, size) != 0)
        safe_copy(buf, size, "unknown");
}

// ============================================================================
// HARDWARE INFORMATION
// ============================================================================

/**
 * Get hardware vendor and model from DMI
 *
 * Reads /sys/devices/virtual/dmi/id/sys_vendor and product_name.
 * Extracts model name from QEMU-style product strings.
 *
 * Buffer sizes for vendor/product are limited to 60 bytes so that
 * snprintf("%s (%s)", vendor, model) always fits in buf[128]:
 *   60 + " (" + 60 + ")" = 124 bytes < 128 — no truncation warning.
 *
 * @param buf   Output buffer
 * @param size  Buffer size
 */
static void get_host(char *buf, size_t size) {
    char vendor[60]  = {0};
    char product[60] = {0};

    FILE *fp = fopen("/sys/devices/virtual/dmi/id/sys_vendor", "r");
    if (fp) {
        if (fgets(vendor, sizeof(vendor), fp))
            vendor[strcspn(vendor, "\n")] = '\0';
        fclose(fp);
    }

    fp = fopen("/sys/devices/virtual/dmi/id/product_name", "r");
    if (fp) {
        if (fgets(product, sizeof(product), fp))
            product[strcspn(product, "\n")] = '\0';
        fclose(fp);
    }

    if (vendor[0] == '\0' && product[0] == '\0') {
        safe_copy(buf, size, "Unknown");
        return;
    }

    /* Extract model from QEMU-style string e.g. "Standard PC (Q35 + ICH9, 2009)" */
    char       model[60] = {0};
    char       *start    = strchr(product, '(');
    const char *end      = strchr(product, ')');

    if (start && end && end > start + 1) {
        size_t len = (size_t)(end - start - 1);
        if (len >= sizeof(model))
            len = sizeof(model) - 1;
        safe_copy(model, sizeof(model), start + 1);
        model[len] = '\0';

        /* Trim " + ICH9, 2009" suffix */
        char *plus = strstr(model, " +");
        if (plus) *plus = '\0';
    }

    if (vendor[0] && model[0])
        snprintf(buf, size, "%s (%s)", vendor, model);
    else if (vendor[0] && product[0])
        snprintf(buf, size, "%s (%s)", vendor, product);
    else if (vendor[0])
        safe_copy(buf, size, vendor);
    else
        safe_copy(buf, size, product);
}

// ============================================================================
// PUBLIC API: FULL STATUS
// ============================================================================

/**
 * Get detailed system status with ASCII progress bars.
 *
 * @param buffer  Output buffer
 * @param size    Buffer size
 * @param req_id  Request ID for log correlation
 * @return        0 on success, -1 on error
 */
int system_get_status(char *buffer, size_t size, unsigned short req_id) {
    LOG_STATE(LOG_DEBUG, "req=%04x system_get_status() called", req_id);

    if (!buffer || size == 0) return -1;

    buffer[0] = '\0';

    char os[128], host[128], hostname[64];
    get_os(os, sizeof(os));
    get_host(host, sizeof(host));
    get_hostname(hostname, sizeof(hostname));

    double l1, l5, l15;
    get_load(&l1, &l5, &l15);

    int used_mem, total_mem, mem_pct;
    get_memory(&used_mem, &total_mem, &mem_pct);

    int used_disk, total_disk, disk_pct;
    get_disk(&used_disk, &total_disk, &disk_pct);

    /* Clamp percentages to valid range */
    if (mem_pct  < 0)   mem_pct  = 0;
    if (mem_pct  > 100) mem_pct  = 100;
    if (disk_pct < 0)   disk_pct = 0;
    if (disk_pct > 100) disk_pct = 100;

    char mem_bar[64], disk_bar[64];
    make_bar(mem_bar,  sizeof(mem_bar),  mem_pct);
    make_bar(disk_bar, sizeof(disk_bar), disk_pct);

    int days, hours, mins;
    get_uptime(&days, &hours, &mins);

    int users = get_user_count();

    int written = snprintf(buffer, size,
        "🖥 *System info*\n\n"
        "```\n"
        "%-8s : %s\n"
        "%-8s : %s\n"
        "%-8s : %s\n\n"
        "%-8s : %dd %dh %dm\n"
        "%-8s : %d online\n\n"
        "CPU Load\n"
        "%-8s : %.2f\n"
        "%-8s : %.2f\n"
        "%-8s : %.2f\n\n"
        "Memory\n"
        "%-8s : %d / %d MB (%d%%)\n"
        "Bar      : %s %d%%\n\n"
        "Disk\n"
        "%-8s : %d / %d GB (%d%%)\n"
        "Bar      : %s %d%%\n"
        "```",
        "OS",       os,
        "Host",     host,
        "Hostname", hostname,
        "Uptime",   days, hours, mins,
        "Users",    users,
        "1m",       l1,
        "5m",       l5,
        "15m",      l15,
        "Used",     used_mem,  total_mem,  mem_pct,  mem_bar,  mem_pct,
        "Used",     used_disk, total_disk, disk_pct, disk_bar, disk_pct
    );

    if (written < 0) {
        LOG_STATE(LOG_ERROR, "req=%04x system_get_status failed", req_id);
        return -1;
    }

    if ((size_t)written >= size)
        LOG_STATE(LOG_WARN, "req=%04x system_get_status truncated", req_id);

    LOG_STATE(LOG_INFO, "req=%04x system status built (len=%d)", req_id, written);
    return 0;
}

// ============================================================================
// PUBLIC API: MINI STATUS
// ============================================================================

/**
 * Get compact system status with emoji heat indicators.
 *
 * Heat indicators:
 *   🟢 Normal   🟡 Warning   🔴 Critical
 *
 * Thresholds defined via SYS_*_WARN/CRIT constants.
 *
 * @param buffer  Output buffer
 * @param size    Buffer size
 * @param req_id  Request ID for log correlation
 * @return        0 on success, -1 on error
 */
int system_get_status_mini(char *buffer, size_t size, unsigned short req_id) {
    LOG_STATE(LOG_DEBUG, "req=%04x system_get_status_mini() called", req_id);

    if (!buffer || size == 0) return -1;

    buffer[0] = '\0';

    double l1, l5, l15;
    get_load(&l1, &l5, &l15);

    int used_mem, total_mem, mem_pct;
    get_memory(&used_mem, &total_mem, &mem_pct);

    int used_disk, total_disk, disk_pct;
    get_disk(&used_disk, &total_disk, &disk_pct);

    if (mem_pct  < 0)   mem_pct  = 0;
    if (mem_pct  > 100) mem_pct  = 100;
    if (disk_pct < 0)   disk_pct = 0;
    if (disk_pct > 100) disk_pct = 100;

    int days, hours, mins;
    get_uptime(&days, &hours, &mins);

    const char *mem_icon =
        (mem_pct  > SYS_MEM_CRIT_PCT)  ? "🔴" :
        (mem_pct  > SYS_MEM_WARN_PCT)  ? "🟡" : "🟢";

    const char *disk_icon =
        (disk_pct > SYS_DISK_CRIT_PCT) ? "🔴" :
        (disk_pct > SYS_DISK_WARN_PCT) ? "🟡" : "🟢";

    const char *cpu_icon =
        (l1 > SYS_CPU_CRIT_LOAD) ? "🔴" :
        (l1 > SYS_CPU_WARN_LOAD) ? "🟡" : "🟢";

    int written = snprintf(buffer, size,
        "⚡ *System Health*\n\n"
        "```\n"
        "%-5s %5.2f (1m) %s\n"
        "%-5s %3d%% (%d/%d MB) %s\n"
        "%-5s %3d%% (%d/%d GB) %s\n"
        "%-5s %dd %dh %dm\n"
        "```",
        "CPU:", l1,       cpu_icon,
        "MEM:", mem_pct,  used_mem,  total_mem,  mem_icon,
        "DSK:", disk_pct, used_disk, total_disk, disk_icon,
        "UP:",  days, hours, mins
    );

    if (written < 0) {
        LOG_STATE(LOG_ERROR, "req=%04x system_get_status_mini failed", req_id);
        return -1;
    }

    if ((size_t)written >= size)
        LOG_STATE(LOG_WARN, "req=%04x system_get_status_mini truncated", req_id);

    LOG_STATE(LOG_INFO, "req=%04x system health status built (len=%d)", req_id, written);
    return 0;
}
