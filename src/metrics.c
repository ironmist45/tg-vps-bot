/**
 * tg-bot - Telegram bot for system administration
 * metrics.c - Bot usage metrics implementation
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "metrics.h"
#include <stdio.h>
#include <string.h>

bot_metrics_t g_metrics = {0};

void metrics_format_log(char *buf, size_t size) {
    snprintf(buf, size,
        "Commands: %lu (start=%lu help=%lu logs=%lu f2b=%lu reboot=%lu "
        "restart=%lu services=%lu users=%lu health=%lu other=%lu) | "
        "Errors: unauthorized=%lu timeout=%lu exec=%lu | "
        "API: %lu calls, %lu failed | "
        "Response: avg=%ldms max=%ldms",
        g_metrics.cmd_total,
        g_metrics.cmd_start, g_metrics.cmd_help, g_metrics.cmd_logs,
        g_metrics.cmd_fail2ban, g_metrics.cmd_reboot, g_metrics.cmd_restart,
        g_metrics.cmd_services, g_metrics.cmd_users, g_metrics.cmd_health,
        g_metrics.cmd_other,
        g_metrics.err_unauthorized, g_metrics.err_timeout, g_metrics.err_exec,
        g_metrics.api_calls_total, g_metrics.api_calls_failed,
        g_metrics.avg_response_ms, g_metrics.max_response_ms);
}

void metrics_format_health(char *buf, size_t size) {
    int pos = snprintf(buf, size,
        "\n📊 *BOT METRICS*\n"
        "`COMMANDS (total: %lu)\n"
        "/start      %lu\n"
        "/help       %lu\n"
        "/logs       %lu\n"
        "/fail2ban   %lu\n"
        "/reboot      %lu\n"
        "/restart     %lu\n"
        "/services    %lu\n"
        "/users       %lu\n"
        "/health      %lu\n"
        "other        %lu\n"
        " `\n"
        "`ERRORS\n"
        "Unauthorized:   %lu\n"
        "Timeouts:       %lu\n"
        "Exec failures:  %lu\n"
        " `\n"
        "`PERFORMANCE\n"
        "Avg response:  %ldms\n"
        "Max response:  %ldms\n"
        "Poll cycles:   %lu\n"
        "API calls:     %lu (%lu failed)`",
        g_metrics.cmd_total,
        g_metrics.cmd_start, g_metrics.cmd_help, g_metrics.cmd_logs,
        g_metrics.cmd_fail2ban, g_metrics.cmd_reboot, g_metrics.cmd_restart,
        g_metrics.cmd_services, g_metrics.cmd_users, g_metrics.cmd_health,
        g_metrics.cmd_other,
        g_metrics.err_unauthorized, g_metrics.err_timeout, g_metrics.err_exec,
        g_metrics.avg_response_ms, g_metrics.max_response_ms,
        g_metrics.poll_count,
        g_metrics.api_calls_total, g_metrics.api_calls_failed);

    if (pos >= (int)size) {
        buf[size - 1] = '\0';
    }
}
