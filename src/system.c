/**
 * tg-bot - Telegram bot for system administration
 * 
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
 * 
 * Exports two main formats:
 *   - system_get_status()      : Full detailed status with ASCII bars
 *   - system_get_status_mini() : Compact status with emoji indicators
 * 
 * MIT License
 * 
 * Copyright (c) 2026
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "system.h"
#include "logger.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utmp.h>
#include <sys/statvfs.h>

// Bot process start time (defined in lifecycle.c)
extern time_t g_start_time;

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
        } else if (strcmp(key, "MemAvailable:") == 0) {
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

    *total_gb = (int)(total / (1024 * 1024 * 1024));
    *used_gb  = (int)(used  / (1024 * 1024 * 1024));

    if (total > 0) {
        *percent = (int)((used * 100) / total);
    } else {
        *percent = 0;
    }

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
    const int width = 20;

    int filled = (percent * width) / 100;
    int empty  = width - filled;

    int pos = 0;

    pos += snprintf(buf + pos, size - pos, "[");

    for (int i = 0; i < filled && pos < (int)size - 1; i++)
        pos += snprintf(buf + pos, size - pos, "█");

    for (int i = 0; i < empty && pos < (int)size - 1; i++)
        pos += snprintf(buf + pos, size - pos, "░");

    snprintf(buf + pos, size - pos, "]");
}

// ============================================================================
// ACTIVE USER COUNT
// ============================================================================

/**
 * Get number of currently logged-in users (from utmp)
 * 
 * @return  Number of active user processes
 */
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
        snprintf(buf, size, "Unknown");
        return;
    }

    char line[256];

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
            char *val = line + 12;

            // Remove trailing newline
            val[strcspn(val, "\n")] = 0;

            // Strip quotes
            if (*val == '"' || *val == '\'') {
                val++;
                char *end = strrchr(val, '"');
                if (end) *end = 0;
            }

            snprintf(buf, size, "%s", val);
            fclose(fp);
            return;
        }
    }

    fclose(fp);
    snprintf(buf, size, "Unknown");
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
 * @param buf   Output buffer
 * @param size  Buffer size
 */
static void get_host(char *buf, size_t size) {
    char vendor[128] = {0};
    char product[128] = {0};

    // Read vendor
    FILE *fp = fopen("/sys/devices/virtual/dmi/id/sys_vendor", "r");
    if (fp) {
        if (fgets(vendor, sizeof(vendor), fp)) {
            vendor[strcspn(vendor, "\n")] = 0;
        }
        fclose(fp);
    }

    // Read product name
    fp = fopen("/sys/devices/virtual/dmi/id/product_name", "r");
    if (fp) {
        if (fgets(product, sizeof(product), fp)) {
            product[strcspn(product, "\n")] = 0;
        }
        fclose(fp);
    }

    // Fallback if both are empty
    if (vendor[0] == '\0' && product[0] == '\0') {
        snprintf(buf, size, "Unknown");
        return;
    }

    // Extract model from QEMU-style product string (e.g., "Standard PC (Q35 + ICH9, 2009)")
    char model[64] = {0};
    char *start = strchr(product, '(');
    char *end   = strchr(product, ')');

    if (start && end && end > start + 1) {
        size_t len = (size_t)(end - start - 1);
        if (len < sizeof(model)) {
            strncpy(model, start + 1, len);
            model[len] = '\0';

            // Trim " + ICH9, 2009" suffix
            char *plus = strstr(model, " +");
            if (plus) *plus = '\0';
        }
    }

    // Build result string
    if (vendor[0] && model[0]) {
        snprintf(buf, size, "%s (%s)", vendor, model);
    } else if (vendor[0] && product[0]) {
        snprintf(buf, size, "%s (%s)", vendor, product);
    } else if (vendor[0]) {
        snprintf(buf, size, "%s", vendor);
    } else {
        snprintf(buf, size, "%s", product);
    }
}

// ============================================================================
// PUBLIC API: FULL STATUS
// ============================================================================

/**
 * Get detailed system status with ASCII progress bars
 * 
 * Output format (Markdown code block):
 *   🖥 *System info*
 *   
 *   ```
 *   OS      : Ubuntu 22.04 LTS
 *   Host    : QEMU (Q35)
 *   
 *   Uptime  : 2d 5h 30m
 *   Users   : 1 online
 *   
 *   CPU Load
 *   1m      : 0.15
 *   5m      : 0.10
 *   15m     : 0.05
 *   
 *   Memory
 *   Used    : 2048 / 4096 MB (50%)
 *   Bar     : [██████████░░░░░░░░░░] 50%
 *   
 *   Disk
 *   Used    : 20 / 100 GB (20%)
 *   Bar     : [████░░░░░░░░░░░░░░░░] 20%
 *   ```
 * 
 * @param buffer  Output buffer
 * @param size    Buffer size
 * @return        0 on success, -1 on error
 */
int system_get_status(char *buffer, size_t size, unsigned short req_id) {
    LOG_STATE(LOG_DEBUG, "req=%04x system_get_status() called", req_id);
    
    if (!buffer || size == 0) {
        return -1;
    }

    buffer[0] = '\0';

    // Gather system information
    char os[128];
    char host[128];
    get_os(os, sizeof(os));
    get_host(host, sizeof(host));

    double l1, l5, l15;
    get_load(&l1, &l5, &l15);

    int used_mem, total_mem, mem_pct;
    get_memory(&used_mem, &total_mem, &mem_pct);

    int used_disk, total_disk, disk_pct;
    get_disk(&used_disk, &total_disk, &disk_pct);
    
    // Clamp percentages to valid range
    if (mem_pct < 0) mem_pct = 0;
    if (mem_pct > 100) mem_pct = 100;
    if (disk_pct < 0) disk_pct = 0;
    if (disk_pct > 100) disk_pct = 100;

    // Generate ASCII progress bars
    char mem_bar[64];
    char disk_bar[64];
    make_bar(mem_bar, sizeof(mem_bar), mem_pct);
    make_bar(disk_bar, sizeof(disk_bar), disk_pct);

    int days, hours, mins;
    get_uptime(&days, &hours, &mins);

    int users = get_user_count();

    // Format output
    int written = snprintf(buffer, size,
        "🖥 *System info*\n\n"
        "```\n"

        "%-7s : %s\n"
        "%-7s : %s\n\n"

        "%-7s : %dd %dh %dm\n"
        "%-7s : %d online\n\n"

        "CPU Load\n"
        "%-7s : %.2f\n"
        "%-7s : %.2f\n"
        "%-7s : %.2f\n\n"

        "Memory\n"
        "%-7s : %d / %d MB (%d%%)\n"
        "Bar     : %s %d%%\n\n"

        "Disk\n"
        "%-7s : %d / %d GB (%d%%)\n"
        "Bar     : %s %d%%\n"

        "```",

        "OS",   os,
        "Host", host,

        "Uptime", days, hours, mins,
        "Users",  users,

        "1m",  l1,
        "5m",  l5,
        "15m", l15,

        "Used", used_mem, total_mem, mem_pct,
                mem_bar, mem_pct,
        "Used", used_disk, total_disk, disk_pct,
                disk_bar, disk_pct
    );

    if (written < 0) {
        LOG_STATE(LOG_ERROR, "req=%04x system_get_status failed", req_id);
        return -1;
    }

    if ((size_t)written >= size) {
        LOG_STATE(LOG_WARN, "req=%04x system_get_status truncated", req_id);
    }

        LOG_STATE(LOG_INFO, "req=%04x system status built (len=%d)", req_id, written);
    return 0;
}

// ============================================================================
// PUBLIC API: MINI STATUS
// ============================================================================

/**
 * Get compact system status with emoji heat indicators
 * 
 * Output format:
 *   ⚡ *System status (mini)*
 *   
 *   CPU  🟢 0.15 (1m)
 *   MEM  🟢 50%
 *   DSK  🟢 20%
 *   UP   2d 5h 30m
 * 
 * Heat indicators:
 *   🟢 Green  : Normal
 *   🟡 Yellow : Warning threshold
 *   🔴 Red    : Critical threshold
 * 
 * @param buffer  Output buffer
 * @param size    Buffer size
 * @return        0 on success, -1 on error
 */
int system_get_status_mini(char *buffer, size_t size, unsigned short req_id) {
    LOG_STATE(LOG_DEBUG, "req=%04x system_get_status_mini() called", req_id);

    if (!buffer || size == 0) {
        return -1;
    }

    buffer[0] = '\0';

    // Gather data
    double l1, l5, l15;
    get_load(&l1, &l5, &l15);

    int used_mem, total_mem, mem_pct;
    get_memory(&used_mem, &total_mem, &mem_pct);

    int used_disk, total_disk, disk_pct;
    get_disk(&used_disk, &total_disk, &disk_pct);

    // Clamp percentages
    if (mem_pct < 0) mem_pct = 0;
    if (mem_pct > 100) mem_pct = 100;
    if (disk_pct < 0) disk_pct = 0;
    if (disk_pct > 100) disk_pct = 100;

    int days, hours, mins;
    get_uptime(&days, &hours, &mins);

    // Determine heat indicators
    const char *mem_icon =
        (mem_pct > 85) ? "🔴" :
        (mem_pct > 60) ? "🟡" : "🟢";

    const char *disk_icon =
        (disk_pct > 90) ? "🔴" :
        (disk_pct > 70) ? "🟡" : "🟢";

    const char *cpu_icon =
        (l1 > 2.0) ? "🔴" :
        (l1 > 1.0) ? "🟡" : "🟢";

    // Format compact output
    int written = snprintf(buffer, size,
        "⚡ *System status (mini)*\n\n"
        "CPU  %s %.2f (1m)\n"
        "MEM  %s %d%%\n"
        "DSK  %s %d%%\n"
        "UP   %dd %dh %dm",
        cpu_icon, l1,
        mem_icon, mem_pct,
        disk_icon, disk_pct,
        days, hours, mins
    );

    if (written < 0) {
        LOG_STATE(LOG_ERROR, "req=%04x system_get_status_mini failed", req_id);
        return -1;
    }

    if ((size_t)written >= size) {
        LOG_STATE(LOG_WARN, "req=%04x system_get_status_mini truncated", req_id);
    }

        LOG_STATE(LOG_INFO, "req=%04x system health status built (len=%d)", req_id, written);
    return 0;
}
