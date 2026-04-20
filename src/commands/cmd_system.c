#include "commands.h"
#include "logger.h"
#include "reply.h"
#include "system.h"
#include "utils.h"
#include "version.h"

#include <stdio.h>
#include <time.h>
#include <unistd.h>

// extern globals
extern time_t g_start_time;

// ==== COMMANDS: General + System info ====
// Bot commands: /start, /status,
// ===========  /status_mini, /about, /ping)

// ==== General: /start command ====

int cmd_start(int argc, char *argv[],
              long chat_id,
              char *resp, size_t size,
              response_type_t *resp_type) {
  
    (void)argc; (void)argv; (void)chat_id;

    if (resp_type) *resp_type = RESP_MARKDOWN;

    char status[1024];

    if (system_get_status(status, sizeof(status)) != 0) {
        snprintf(status, sizeof(status), "⚠️ Failed to get system info");
    }

    int written = snprintf(resp, size,
        "*🚀 %s v%s (%s)*\n\n%s\n\n👉 /help",
        APP_NAME, APP_VERSION, APP_CODENAME, status);
    if (written < 0 || (size_t)written >= size)
        return -1;

    return 0;
}

// ==== System info: /status command ====

int cmd_status(int argc, char *argv[],
                   long chat_id,
                   char *resp, size_t size,
                   response_type_t *resp_type) {
  
    (void)argc;      // unused
    (void)chat_id;  // unused

    if (resp_type) *resp_type = RESP_MARKDOWN;

    if (validate_command(argv, resp, size) != 0)
        return -1;
  
    if (system_get_status(resp, size) != 0) {
        snprintf(resp, size, "⚠️ Failed to get system status");
        return -1;
    }
  
return 0;
  
}

// ==== General: /status_mini command ====

int cmd_status_mini(int argc, char *argv[],
                        long chat_id,
                        char *resp, size_t size,
                        response_type_t *resp_type) {
  
    (void)argc;      // unused
    (void)chat_id;  // unused

    if (resp_type) *resp_type = RESP_MARKDOWN;

    if (validate_command(argv, resp, size) != 0)
        return -1;

    if (system_get_status_mini(resp, size) != 0) {
        snprintf(resp, size, "⚠️ Failed to get system status");
        return -1;
    }

    return 0;
}

// ===== System info: /about command =====

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

// ===== System info: /ping command =====

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

int cmd_ping_v2(command_ctx_t *ctx)
{
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // ===== inbound latency =====
    long inbound_ms = -1;

    if (ctx->msg_date > 0) {
        time_t now = time(NULL);
        inbound_ms = (now - ctx->msg_date) * 1000;
    }

    // ===== uptime =====
    char uptime[64];
    system_get_uptime_str(uptime, sizeof(uptime));

    // ===== processing time =====
    clock_gettime(CLOCK_MONOTONIC, &end);

    long processing_ms =
        (end.tv_sec - start.tv_sec) * 1000 +
        (end.tv_nsec - start.tv_nsec) / 1000000;

    // ===== RTT (estimated) =====
    long rtt_ms = -1;
    if (inbound_ms >= 0)
        rtt_ms = inbound_ms + processing_ms;

    // ===== format strings =====
    char inbound_str[32];
    char rtt_str[32];

    // inbound
    if (inbound_ms < 0) {
        snprintf(inbound_str, sizeof(inbound_str), "N/A");
    } else if (inbound_ms == 0) {
        snprintf(inbound_str, sizeof(inbound_str), "<1 ms");
    } else {
        snprintf(inbound_str, sizeof(inbound_str), "%ld ms", inbound_ms);
    }

    // rtt
    if (rtt_ms < 0) {
        snprintf(rtt_str, sizeof(rtt_str), "N/A");
    } else if (rtt_ms == 0) {
        snprintf(rtt_str, sizeof(rtt_str), "<1 ms");
    } else {
        snprintf(rtt_str, sizeof(rtt_str), "%ld ms", rtt_ms);
    }

    // ===== LOG =====
    LOG_CTX(LOG_CMD, ctx, LOG_INFO,
        "ping: inbound=%s processing=%ld ms rtt=%s uptime=%s",
        inbound_str,
        processing_ms,
        rtt_str,
        uptime);

    // ===== RESPONSE =====
    char msg[256];

    snprintf(msg, sizeof(msg),
        "🏓 PONG\n\n"
        "Status: OK\n"
        "Processing: %ld ms\n"
        "Inbound: %s\n"
        "RTT (est): %s\n"
        "Uptime: %s",
        processing_ms,
        inbound_str,
        rtt_str,
        uptime);

    return reply_markdown(ctx, msg);
}
