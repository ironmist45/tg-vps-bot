#include "commands.h"
#include "logger.h"
#include "system.h"
#include "version.h"

#include <stdio.h>
#include <time.h>
#include <unistd.h>

// extern globals
extern time_t g_start_time;

// ===== /about =====
int cmd_about(int argc, char *argv[],
              long chat_id,
              char *resp, size_t size,
              response_type_t *resp_type) {

    (void)argc; (void)argv; (void)chat_id;

    if (resp_type) *resp_type = RESP_MARKDOWN;

    time_t now = time(NULL);
    long uptime = now - g_start_time;

    int days = uptime / 86400;
    int hours = (uptime % 86400) / 3600;
    int mins = (uptime % 3600) / 60;

    int written = snprintf(resp, size,
        "*ℹ️ ABOUT*\n\n"
        "%s v%s (%s)\n"
        "PID: %d\n"
        "Uptime: %dd %dh %dm",
        APP_NAME, APP_VERSION, APP_CODENAME,
        getpid(), days, hours, mins);

    if (written < 0 || (size_t)written >= size)
        return -1;

    return 0;
}

// ===== /ping =====
int cmd_ping(int argc, char *argv[],
             long chat_id,
             char *resp, size_t size,
             response_type_t *resp_type) {

    (void)argc; (void)argv; (void)chat_id;

    if (resp_type) *resp_type = RESP_MARKDOWN;

    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    clock_gettime(CLOCK_MONOTONIC, &end);

    long latency_ms =
        (end.tv_sec - start.tv_sec) * 1000 +
        (end.tv_nsec - start.tv_nsec) / 1000000;

    time_t now = time(NULL);
    long uptime = now - g_start_time;

    int mins = (uptime % 3600) / 60;
    int secs = uptime % 60;

    LOG_STATE(LOG_INFO,
        "ping: latency=%ld ms uptime=%dm%ds",
        latency_ms, mins, secs);

    int written = snprintf(resp, size,
        "*🏓 PING*\n\n"
        "Status: `OK`\n"
        "Latency: `%ld ms`\n"
        "Uptime: `%dm %ds`",
        latency_ms, mins, secs);

    if (written < 0 || (size_t)written >= size)
        return -1;

    return 0;
}
