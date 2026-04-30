/**
 * tg-bot - Telegram bot for system administration
 * metrics.h - Bot usage metrics and statistics
 * MIT License - Copyright (c) 2026 ironmist45
 */

#ifndef METRICS_H
#define METRICS_H

#include <stddef.h>

// ============================================================================
// СТРУКТУРА МЕТРИК
// ============================================================================

typedef struct {
    // --- Команды ---
    unsigned long cmd_total;
    unsigned long cmd_start;
    unsigned long cmd_help;
    unsigned long cmd_logs;
    unsigned long cmd_fail2ban;
    unsigned long cmd_reboot;
    unsigned long cmd_restart;
    unsigned long cmd_services;
    unsigned long cmd_users;
    unsigned long cmd_health;
    unsigned long cmd_other;

    // --- Ошибки ---
    unsigned long err_unauthorized;
    unsigned long err_timeout;
    unsigned long err_exec;

    // --- Производительность ---
    long avg_response_ms;
    long max_response_ms;
    unsigned long poll_count;

    // --- Telegram API ---
    unsigned long api_calls_total;
    unsigned long api_calls_failed;
} bot_metrics_t;

extern bot_metrics_t g_metrics;

// ============================================================================
// МАКРОСЫ
// ============================================================================

#define METRICS_CMD(name) do { \
    g_metrics.cmd_total++; \
    g_metrics.cmd_##name++; \
} while(0)

// ============================================================================
// ФУНКЦИИ
// ============================================================================

void metrics_format_log(char *buf, size_t size);
void metrics_format_health(char *buf, size_t size);

#endif // METRICS_H
