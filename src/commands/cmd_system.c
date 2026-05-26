/**
 * tg-bot - Telegram bot for system administration
 * cmd_system.c - System information command handlers (/start, /status, /health, /about, /ping, /logstat)
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "build_info.h"
#include "commands.h"
#include "logger.h"
#include "reply.h"
#include "system.h"
#include "utils.h"
#include "version.h"
#include "logstat.h"
#include "config.h"
#include "metrics.h"
#include "telegram_parser.h"
#include "telegram_poll.h"

#include <curl/curl.h>
#include <openssl/opensslv.h>
#include <openssl/crypto.h>
#include <cjson/cJSON.h>
#include <ares.h>
#include <ares_version.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Bot process start time (defined in lifecycle.c)
extern time_t g_start_time;

// ============================================================================
// SELFCHECK
// ============================================================================

/*
 * Run a lightweight self-diagnostic and format result into buf.
 *
 * Checks:
 *   - Log file is writable (access W_OK)
 *   - Combined RSS from g_poll_rss_kb / g_poll_rss_procs, sampled by
 *     telegram_poll() right after fork() while both processes are alive.
 *
 * Output examples:
 *   "✅ OK (RSS: 16.1 MB, 2 procs)"
 *   "✅ OK (RSS: 8.9 MB, 1 proc)"
 *   "⚠️ log unavailable (RSS: 16.1 MB, 2 procs)"
 */
static void selfcheck(char *buf, size_t size)
{
    /* Check log file writability */
    int log_ok = (access(g_cfg.log_file, W_OK) == 0);

    const char *status = log_ok ? "✅ OK" : "⚠️ log unavailable";

    if (g_poll_rss_kb > 0) {
        snprintf(buf, size, "%s (RSS: %.1f MB, %d %s)",
                 status,
                 (double)g_poll_rss_kb / 1024.0,
                 g_poll_rss_procs,
                 g_poll_rss_procs == 1 ? "proc" : "procs");
    } else {
        /* No RSS data yet (first /start before first poll cycle) */
        snprintf(buf, size, "%s", status);
    }
}

// ============================================================================
// /start COMMAND (V2)
// ============================================================================

/**
 * Bot introduction and welcome message
 *
 * Displays:
 *   - Bot name, version, codename
 *   - Personalized greeting (if username known)
 *   - Self-diagnostic (RSS, log file check)
 *   - Bot uptime
 *   - Quick navigation hints
 *
 * @param ctx  Command context
 * @return     0 on success
 */
int cmd_start_v2(command_ctx_t *ctx)
{
    /* Run selfcheck */
    char check[64];
    selfcheck(check, sizeof(check));

    /* Get bot uptime */
    char uptime[64];
    system_get_uptime_str(uptime, sizeof(uptime));

    LOG_CMD_CTX(ctx, LOG_INFO, "start: user opened bot");
    METRICS_CMD(start);

    char msg[512];
    /* Escape username — may contain MarkdownV2 special chars */
    char safe_username[128] = {0};
    if (ctx->username)
        telegram_escape_markdown(ctx->username, safe_username, sizeof(safe_username));
    /* Escape check — contains dots in RSS value (e.g. "8.5 MB") */
    char safe_check[128] = {0};
    telegram_escape_markdown(check, safe_check, sizeof(safe_check));
    /* Escape version — contains dots (e.g. "1.2.6") */
    char safe_version[32] = {0};
    telegram_escape_markdown(APP_VERSION, safe_version, sizeof(safe_version));

    snprintf(msg, sizeof(msg),
        "🚀 *tg\\-bot v%s \\(%s\\)*\n\n"
        "Welcome%s%s\\!\n\n"
        "*System:* %s\n"
        "*Uptime:* %s\n\n"
        "👉 /help — commands\n"
        "👉 /status — full status\n"
        "👉 /health — quick check",
        safe_version, APP_CODENAME,
        ctx->username ? " @" : "",
        ctx->username ? safe_username : "",
        safe_check,
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
    METRICS_CMD(status);

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
    METRICS_CMD(health);

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
    time_t now    = time(NULL);
    long   uptime = now - g_start_time;

    int days  = uptime / 86400;
    int hours = (uptime % 86400) / 3600;
    int mins  = (uptime % 3600) / 60;

    /* Escape version — contains dots (e.g. "1.2.6") */
    char safe_version[32] = {0};
    telegram_escape_markdown(APP_VERSION, safe_version, sizeof(safe_version));

    char msg[512];
    snprintf(msg, sizeof(msg),
        "*ℹ️ ABOUT*\n\n"
        "*tg\\-bot v%s \\(%s\\)*\n"
        "*Main PID:* %d\n"
        "*Bot uptime:* %dd %dh %dm\n"
        "*Build:* \\#%s\n"
        "*Commit:* %s\n"
        "*Built:* `%s`\n"
        "*libcurl:* `%.14s`\n"
        "*OpenSSL:* `%s`\n"
        "*c\\-ares:* `%s`\n"
        "*cJSON:* `%d.%d.%d`",
        safe_version, APP_CODENAME,
        getpid(), days, hours, mins,
        TG_BUILD_NUMBER, TG_BUILD_COMMIT, TG_BUILD_DATE,
        curl_version(), OpenSSL_version(0),
        ares_version(NULL),
        CJSON_VERSION_MAJOR, CJSON_VERSION_MINOR, CJSON_VERSION_PATCH);
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
        time_t now  = time(NULL);
        time_t diff = now - ctx->msg_date;

        /* Guard against overflow: diff * 1000 must fit in long */
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

    if (processing_ms > 5000 || inbound_ms > 10000) {
        status_text  = "Slow";
        status_emoji = "⚠️";
    } else if (processing_ms > 1000 || inbound_ms > 5000) {
        status_text  = "Laggy";
        status_emoji = "🟡";
    } else {
        status_text  = "OK";
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

    METRICS_CMD(ping);
    return reply_plain(ctx, msg);
}

// ============================================================================
// /logstat COMMAND (V2)
// ============================================================================

/**
 * Display bot log file statistics
 *
 * Analyzes /var/log/tg-bot.log using SSE4.2-accelerated newline counting.
 * Shows total lines, level breakdown, top tags, and busiest hour.
 *
 * @param ctx  Command context
 * @return     0 on success, error reply on failure
 */
int cmd_logstat_v2(command_ctx_t *ctx)
{
    logstat_result_t result;

    if (logstat_analyze(g_cfg.log_file, &result) != 0)
        return reply_error(ctx, "Failed to analyze log file");

    char buf[1024];
    if (logstat_format(&result, buf, sizeof(buf)) != 0)
        return reply_error(ctx, "Failed to format statistics");

    LOG_CMD_CTX(ctx, LOG_INFO,
        "logstat: analyzed %zu bytes, %ld lines, %ld errors, %ld warnings",
        result.file_size, result.total_lines,
        result.level_error, result.level_warn);
    METRICS_CMD(logstat);

    return reply_markdown(ctx, buf);
}
