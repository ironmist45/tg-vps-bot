/**
 * tg-bot - Telegram bot for system administration
 * cmd_system.c - System information command handlers (/start, /status, /health, /about, /ping)
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "build_info.h"
#include "commands.h"
#include "logger.h"
#include "reply.h"
#include "system.h"
#include "utils.h"
#include "version.h"
#include "metrics.h"

#include <curl/curl.h>
#include <openssl/opensslv.h>
#include <openssl/crypto.h>
#include <cjson/cJSON.h>
#include <ares.h>
#include <ares_version.h>

#include <limits.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

// Bot process start time (defined in lifecycle.c)
extern time_t g_start_time;

// ============================================================================
// /start COMMAND (V2)
// ============================================================================

/**
 * Bot introduction and welcome message
 * 
 * Displays:
 *   - Bot name, version, codename
 *   - Personalized greeting (if username known)
 *   - System status summary (mini)
 *   - Bot uptime
 *   - Quick navigation hints
 * 
 * @param ctx  Command context
 * @return     0 on success
 */
int cmd_start_v2(command_ctx_t *ctx)
{
    // Get compact system status
    char status[512];
    if (system_get_status_mini(status, sizeof(status), ctx->req_id) != 0) {
        snprintf(status, sizeof(status), "⚠️ System info unavailable");
    }

    // Get bot uptime
    char uptime[64];
    system_get_uptime_str(uptime, sizeof(uptime));

    LOG_CMD_CTX(ctx, LOG_INFO, "start: user opened bot");
    METRICS_CMD("start");

    // Format welcome message
    char msg[512];
    snprintf(msg, sizeof(msg),
        "🚀 *%s v%s (%s)*\n\n"
        "Welcome%s%s!\n\n"
        "*System:* OK\n"
        "*Uptime:* %s\n\n"
        "👉 /help — commands\n"
        "👉 /status — full status\n"
        "👉 /health — quick check",
        APP_NAME, APP_VERSION, APP_CODENAME,
        ctx->username ? " @" : "",
        ctx->username ? ctx->username : "",
        uptime
    );

    return reply_markdown(ctx, msg);
}

// ============================================================================
// /status COMMAND (V2)
// ============================================================================

/**
 * Display detailed system status
 * 
 * Shows comprehensive system information including:
 *   - OS and hardware info
 *   - CPU load averages (1m, 5m, 15m)
 *   - Memory usage with progress bar
 *   - Disk usage with progress bar
 *   - System uptime
 *   - Active user count
 * 
 * Output format: Markdown code block with ASCII progress bars
 * 
 * @param ctx  Command context
 * @return     0 on success, error reply on failure
 */
int cmd_status_v2(command_ctx_t *ctx)
{
    char buf[1024];

    if (system_get_status(buf, sizeof(buf), ctx->req_id) != 0) {
        return reply_markdown(ctx, "⚠️ Failed to get system status");
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "status: requested");

    return reply_markdown(ctx, buf);
}

// ============================================================================
// /health COMMAND (V2)
// ============================================================================

/**
 * Quick system health check (compact format)
 * 
 * Shows essential metrics with emoji heat indicators:
 *   - CPU load (🟢 normal, 🟡 warning, 🔴 critical)
 *   - Memory usage percentage
 *   - Disk usage percentage
 *   - System uptime
 *   - Bot metrics (commands, errors, performance, API stats)
 * 
 * @param ctx  Command context
 * @return     0 on success, error reply on failure
 */
int cmd_health_v2(command_ctx_t *ctx)
{
    char buf[512];

    if (system_get_status_mini(buf, sizeof(buf), ctx->req_id) != 0) {
        return reply_markdown(ctx, "⚠️ Failed to get health status");
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "health: requested");
    METRICS_CMD("health");

    char health_buf[1024];
    snprintf(health_buf, sizeof(health_buf), "%s\n", buf);
    metrics_format_health(health_buf + strlen(health_buf),
                          sizeof(health_buf) - strlen(health_buf));

    return reply_markdown(ctx, health_buf);
}

// ============================================================================
// /about COMMAND (V2)
// ============================================================================

/**
 * Display bot version and runtime information
 * 
 * Shows:
 *   - Application name, version, codename
 *   - Process ID (PID)
 *   - Bot uptime (since process start)
 *   - Build commit hash (short)
 *   - Build date and time (UTC)
 *   - libcurl version
 *   - OpenSSL version
 *   - cJSON version
 * 
 * @param ctx  Command context
 * @return     0 on success
 */
int cmd_about_v2(command_ctx_t *ctx)
{
    time_t now = time(NULL);
    long uptime = now - g_start_time;

    int days = uptime / 86400;
    int hours = (uptime % 86400) / 3600;
    int mins = (uptime % 3600) / 60;

    char msg[256];
    snprintf(msg, sizeof(msg),
        "*ℹ️ ABOUT*\n\n"
        "%s v%s (%s)\n"
        "*PID:* %d\n"
        "*Uptime:* %dd %dh %dm\n"
        "*Commit:* %s\n"
        "*Built:* %s\n"
        "*libcurl:* %.14s\n"
        "*OpenSSL:* %s\n"
        "*c-ares:* %s\n"
        "*cJSON:* %d.%d.%d",
        APP_NAME, APP_VERSION, APP_CODENAME,
        getpid(), days, hours, mins,
        TG_BUILD_COMMIT, TG_BUILD_DATE,
        curl_version(), OpenSSL_version(0),
        ares_version(NULL),
        CJSON_VERSION_MAJOR, CJSON_VERSION_MINOR, CJSON_VERSION_PATCH);
    LOG_CMD_CTX(ctx, LOG_INFO, "about: requested");

    return reply_markdown(ctx, msg);
}

// ============================================================================
// /ping COMMAND (V2)
// ============================================================================

/**
 * Network and processing latency test
 * 
 * Measures and reports:
 *   - Processing time: command handler execution time
 *   - Inbound latency: Telegram → Bot (estimated from message timestamp)
 *   - RTT (estimated): round-trip time Telegram → Bot → Telegram
 *   - Bot uptime
 * 
 * Note: Latency measurements are estimates due to clock skew.
 * 
 * @param ctx  Command context (uses msg_date for latency calculation)
 * @return     0 on success
 */
int cmd_ping_v2(command_ctx_t *ctx)
{
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // ------------------------------------------------------------------------
    // Calculate inbound latency (Telegram → Bot)
    // ------------------------------------------------------------------------
    long inbound_ms = -1;
    if (ctx->msg_date > 0) {
        time_t now = time(NULL);
        time_t diff = now - ctx->msg_date;
        
        // Защита от переполнения: если разница > LONG_MAX / 1000
        if (diff > LONG_MAX / 1000) {
            inbound_ms = LONG_MAX;
        } else {
            inbound_ms = (long)(diff * 1000);
        }
    }
    
    // ------------------------------------------------------------------------
    // Get bot uptime
    // ------------------------------------------------------------------------
    char uptime[64];
    system_get_uptime_str(uptime, sizeof(uptime));

    // ------------------------------------------------------------------------
    // Calculate processing time
    // ------------------------------------------------------------------------
    clock_gettime(CLOCK_MONOTONIC, &end);
    long processing_ms = elapsed_ms(start, end);

    // ------------------------------------------------------------------------
    // Estimate round-trip time
    // ------------------------------------------------------------------------
    long rtt_ms = -1;
    if (inbound_ms >= 0)
        rtt_ms = inbound_ms + processing_ms;

    // ------------------------------------------------------------------------
    // Format latency strings
    // ------------------------------------------------------------------------
    char inbound_str[32];
    char rtt_str[32];

    if (inbound_ms < 0) {
        snprintf(inbound_str, sizeof(inbound_str), "N/A");
    } else if (inbound_ms == 0) {
        snprintf(inbound_str, sizeof(inbound_str), "<1 ms");
    } else {
        snprintf(inbound_str, sizeof(inbound_str), "%ld ms", inbound_ms);
    }

    if (rtt_ms < 0) {
        snprintf(rtt_str, sizeof(rtt_str), "N/A");
    } else if (rtt_ms == 0) {
        snprintf(rtt_str, sizeof(rtt_str), "<1 ms");
    } else {
        snprintf(rtt_str, sizeof(rtt_str), "%ld ms", rtt_ms);
    }

    LOG_CMD_CTX(ctx, LOG_INFO,
        "ping: inbound=%s processing=%ld ms rtt=%s uptime=%s",
        inbound_str, processing_ms, rtt_str, uptime);

    // ------------------------------------------------------------------------
    // Determine status based on real metrics
    // ------------------------------------------------------------------------
    const char *status_text;
    const char *status_emoji;
    
    if (processing_ms > 5000 || (inbound_ms > 0 && inbound_ms > 10000)) {
        status_text = "Slow";
        status_emoji = "⚠️";
    } else if (processing_ms > 1000 || (inbound_ms > 0 && inbound_ms > 5000)) {
        status_text = "Laggy";
        status_emoji = "🟡";
    } else {
        status_text = "OK";
        status_emoji = "🟢";
    }
    
    // ------------------------------------------------------------------------
    // Format response
    // ------------------------------------------------------------------------
    char msg[256];
    snprintf(msg, sizeof(msg),
        "🏓 PONG\n\n"
        "Status: %s %s\n"
        "Processing: %ld ms\n"
        "Inbound: %s\n"
        "RTT (est): %s\n"
        "Uptime: %s",
        status_text, status_emoji,
        processing_ms, inbound_str, rtt_str, uptime);

    return reply_markdown(ctx, msg);
}
