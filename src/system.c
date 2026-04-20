#include "system.h"
#include "logger.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utmp.h>
#include <sys/statvfs.h>

extern time_t g_start_time;

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

int system_get_uptime_str(char *buf, size_t size)
{
    time_t now = time(NULL);
    int seconds = (int)(now - g_start_time);

    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;

    snprintf(buf, size, "%dh %dm %ds", h, m, s);
    return 0;
}

// ===== helpers =====

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

// ===== OS =====

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

            // убрать кавычки и \n
            val[strcspn(val, "\n")] = 0;

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

// ===== HOST =====

static void get_host(char *buf, size_t size) {
    char vendor[128] = {0};
    char product[128] = {0};

    // ===== read vendor =====
    FILE *fp = fopen("/sys/devices/virtual/dmi/id/sys_vendor", "r");
    if (fp) {
        if (fgets(vendor, sizeof(vendor), fp)) {
            vendor[strcspn(vendor, "\n")] = 0;
        }
        fclose(fp);
    }

    // ===== read product =====
    fp = fopen("/sys/devices/virtual/dmi/id/product_name", "r");
    if (fp) {
        if (fgets(product, sizeof(product), fp)) {
            product[strcspn(product, "\n")] = 0;
        }
        fclose(fp);
    }

    // ===== fallback =====
    if (vendor[0] == '\0' && product[0] == '\0') {
        snprintf(buf, size, "Unknown");
        return;
    }

    // ===== extract Q35 =====
    char model[64] = {0};

    char *start = strchr(product, '(');
    char *end   = strchr(product, ')');

    if (start && end && end > start + 1) {
        size_t len = (size_t)(end - start - 1);
        if (len < sizeof(model)) {
            strncpy(model, start + 1, len);
            model[len] = '\0';

            // 👉 обрезаем " + ICH9, 2009"
            char *plus = strstr(model, " +");
            if (plus) *plus = '\0';
        }
    }

    // ===== build result =====
    if (vendor[0] && model[0]) {
        snprintf(buf, size, "%s (%s)", vendor, model);
    }
    else if (vendor[0] && product[0]) {
        snprintf(buf, size, "%s (%s)", vendor, product);
    }
    else if (vendor[0]) {
        snprintf(buf, size, "%s", vendor);
    }
    else {
        snprintf(buf, size, "%s", product);
    }
}

// ===== Main API =====

int system_get_status(char *buffer, size_t size) {

    LOG_STATE(LOG_DEBUG, "system_get_status() called");
    
    if (!buffer || size == 0) {
        return -1;
    }

    buffer[0] = '\0';

    // ✅ Контекст номер 1 (Основная инфа о системе)
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
    
    // ✅ Clamp: защита от "мусора"
    if (mem_pct < 0) mem_pct = 0;
    if (mem_pct > 100) mem_pct = 100;

    if (disk_pct < 0) disk_pct = 0;
    if (disk_pct > 100) disk_pct = 100;

    // ✅ Контекст номер 2 (строим ASCII бары)
    char mem_bar[64];
    char disk_bar[64];

    make_bar(mem_bar, sizeof(mem_bar), mem_pct);
    make_bar(disk_bar, sizeof(disk_bar), disk_pct);

    int days, hours, mins;
    get_uptime(&days, &hours, &mins);

    int users = get_user_count();

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
        "%-7s : %d / %d MB (%d%%)\n\n"
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

int system_get_status_mini(char *buffer, size_t size) {

    LOG_STATE(LOG_DEBUG, "system_get_status_mini() called");

    if (!buffer || size == 0) {
        return -1;
    }

    buffer[0] = '\0';

    // ===== данные =====
    double l1, l5, l15;
    get_load(&l1, &l5, &l15);

    int used_mem, total_mem, mem_pct;
    get_memory(&used_mem, &total_mem, &mem_pct);

    int used_disk, total_disk, disk_pct;
    get_disk(&used_disk, &total_disk, &disk_pct);

    // clamp
    if (mem_pct < 0) mem_pct = 0;
    if (mem_pct > 100) mem_pct = 100;

    if (disk_pct < 0) disk_pct = 0;
    if (disk_pct > 100) disk_pct = 100;

    int days, hours, mins;
    get_uptime(&days, &hours, &mins);

    // ===== heat indicators =====
    const char *mem_icon =
        (mem_pct > 85) ? "🔴" :
        (mem_pct > 60) ? "🟡" : "🟢";

    const char *disk_icon =
        (disk_pct > 90) ? "🔴" :
        (disk_pct > 70) ? "🟡" : "🟢";

    const char *cpu_icon =
        (l1 > 2.0) ? "🔴" :
        (l1 > 1.0) ? "🟡" : "🟢"; // TODO: можно учитывать количество CPU (get_nprocs())

    // ===== ultra-compact output =====
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
        LOG_STATE(LOG_ERROR, "system_get_status_mini failed");
        return -1;
    }

    if ((size_t)written >= size) {
        LOG_STATE(LOG_WARN, "system_get_status_mini truncated");
    }

    LOG_STATE(LOG_INFO, "system status built (len=%d)", written);

    return 0;
}
