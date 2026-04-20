/**
 * tg-bot - Telegram bot for system administration
 * 
 * lifecycle.h - Process lifecycle management interface
 * 
 * Public API for:
 *   - Signal handlers registration
 *   - Shutdown/restart/reboot orchestration
 *   - State flags access (shutdown_requested, reload_config)
 *   - Uptime logging
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

#ifndef LIFECYCLE_H
#define LIFECYCLE_H

#include <time.h>
#include <signal.h>

// ===== Глобальные переменные (экспортируем для main.c) =====

/** Process start time (used for uptime calculation) */
extern time_t g_start_time;

/** Shutdown mode: 0 = none, 1 = restart, 2 = reboot */
extern volatile sig_atomic_t g_shutdown_requested;

/** Config reload flag (set by SIGHUP) */
extern volatile sig_atomic_t g_reload_config;

/** Chat ID that requested reboot/restart (0 if triggered by signal) */
extern long g_reboot_requested_by;

// ===== Инициализация и регистрация обработчиков сигналов =====

/**
 * Initialize lifecycle module state
 * Must be called once at program startup.
 */
void lifecycle_init(void);

/**
 * Register signal handlers (SIGHUP, SIGTERM, SIGINT)
 * Must be called after lifecycle_init().
 */
void lifecycle_register_handlers(void);

// ===== Основная функция shutdown =====

/**
 * Handle shutdown request (reboot or restart)
 * Called from main loop when lifecycle_shutdown_requested() returns true.
 * This function does not return on success.
 */
void lifecycle_handle_shutdown(void);

// ===== Внешнее управление =====

/**
 * Request shutdown from external module (e.g., from Telegram command)
 * 
 * @param mode         1 = restart, 2 = reboot
 * @param requested_by chat_id of user who requested the action
 */
void lifecycle_request_shutdown(int mode, long requested_by);

// ===== Проверка флагов для использования в главном цикле =====

/**
 * Check if shutdown was requested
 * @return 1 if shutdown_requested != 0, 0 otherwise
 */
int lifecycle_shutdown_requested(void);

/**
 * Check if config reload was requested (SIGHUP received)
 * @return 1 if reload_config != 0, 0 otherwise
 */
int lifecycle_reload_requested(void);

/**
 * Clear reload flag after processing
 * Should be called after config reload is complete.
 */
void lifecycle_clear_reload(void);

// ===== Логирование =====

/**
 * Log current uptime in human-readable format
 * Format: "Uptime: Xd Yh Zm"
 */
void lifecycle_log_uptime(void);

#endif // LIFECYCLE_H
