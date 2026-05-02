/**
 * tg-bot - Telegram bot for system administration
 * lifecycle.h - Process lifecycle management interface
 * MIT License - Copyright (c) 2026 ironmist45
 */

#ifndef LIFECYCLE_H
#define LIFECYCLE_H

#include <time.h>
#include <signal.h>

// ==== Shutdown modes ====
#define SHUTDOWN_STOP    0  // SIGTERM from systemd (clean exit)
#define SHUTDOWN_RESTART 1  // /restart command (exec restart)
#define SHUTDOWN_REBOOT  2  // /reboot command (system reboot)

// ===== Глобальные переменные =====

extern time_t g_start_time;
extern volatile sig_atomic_t g_shutdown_requested;
extern volatile sig_atomic_t g_reload_config;
extern volatile sig_atomic_t g_rotate_log;
extern long g_reboot_requested_by;
extern volatile sig_atomic_t g_signal_received;   // ← ДОБАВЛЕНО

// ===== Инициализация =====

void lifecycle_init(void);
void lifecycle_register_handlers(void);

// ===== Shutdown =====

void lifecycle_handle_shutdown(void);
void lifecycle_request_shutdown(int mode, long requested_by);

// ===== Проверка флагов =====

int lifecycle_shutdown_requested(void);
int lifecycle_reload_requested(void);
int lifecycle_rotate_requested(void);
void lifecycle_clear_rotate(void);
void lifecycle_clear_reload(void);

#endif // LIFECYCLE_H
