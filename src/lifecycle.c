/**
 * tg-bot - Telegram bot for system administration
 * 
 * lifecycle.c - Process lifecycle management
 * 
 * Handles:
 *   - Signal handlers (SIGHUP, SIGTERM, SIGINT)
 *   - Graceful shutdown sequence
 *   - System reboot and process restart logic
 *   - Uptime tracking and logging
 * 
 * This module centralizes all shutdown/restart logic, keeping main.c clean
 * and focused on orchestration.
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

#include "environment.h"
#include "lifecycle.h"
#include "logger.h"
#include "telegram.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/reboot.h>
#include <time.h>
#include <stdlib.h>   // ← используется для getenv()

// ===== Глобальные переменные =====
time_t g_start_time;
volatile sig_atomic_t g_shutdown_requested = 0;
volatile sig_atomic_t g_reload_config = 0;
long g_reboot_requested_by = 0;

// ===== Внутренние статические переменные =====
static volatile sig_atomic_t g_signal_received = 0;

// ===== Прототипы внутренних функций =====
static void handle_sighup(int sig);
static void handle_sigterm(int sig);
static void graceful_shutdown(void);

// ===== Реализация =====

void lifecycle_init(void) {
    g_start_time = time(NULL);
    g_shutdown_requested = 0;
    g_reload_config = 0;
    g_reboot_requested_by = 0;
    g_signal_received = 0;
}

void lifecycle_register_handlers(void) {
    struct sigaction sa = {0};
    sa.sa_handler = handle_sighup;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);

    struct sigaction sa_term = {0};
    sa_term.sa_handler = handle_sigterm;
    sa_term.sa_flags = 0;
    sigaction(SIGTERM, &sa_term, NULL);
    sigaction(SIGINT, &sa_term, NULL);
}

int lifecycle_shutdown_requested(void) {
    return g_shutdown_requested != 0;
}

int lifecycle_reload_requested(void) {
    return g_reload_config != 0;
}

void lifecycle_clear_reload(void) {
    g_reload_config = 0;
}

void lifecycle_request_shutdown(int mode, long requested_by) {
    if (g_signal_received) return;
    g_signal_received = 1;
    
    g_shutdown_requested = mode;
    g_reboot_requested_by = requested_by;
    
    LOG_SYS(LOG_WARN, "Shutdown requested (mode=%d) by chat_id=%ld", 
            mode, requested_by);
}

void lifecycle_log_uptime(void) {
    time_t now = time(NULL);
    long uptime = now - g_start_time;

    int days = uptime / 86400;
    int hours = (uptime % 86400) / 3600;
    int mins = (uptime % 3600) / 60;

    LOG_SYS(LOG_INFO, "Uptime: %dd %dh %dm", days, hours, mins);
}

// ===== Обработчики сигналов =====

static void handle_sighup(int sig) {
    (void)sig;
    g_reload_config = 1;
}

static void handle_sigterm(int sig) {
    (void)sig;

    if (g_signal_received) return;
    g_signal_received = 1;

    LOG_SYS(LOG_WARN, "Received SIGTERM, shutting down...");
    g_shutdown_requested = SHUTDOWN_STOP;  // ← 0 = обычный выход
}

// ===== Graceful shutdown =====

static void graceful_shutdown(void) {
    static int done = 0;
    if (done) return;
    done = 1;

    // Блокируем сигналы на время shutdown
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    LOG_SYS(LOG_INFO, "Graceful shutdown: stopping services...");

    telegram_shutdown();
    usleep(200000);

    LOG_SYS(LOG_INFO, "Flushing logs...");
    fflush(NULL);
    
    LOG_SYS(LOG_INFO, "Shutdown sequence complete");
    fflush(NULL);
    sync();

    sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

// ===== Основная функция shutdown =====

void lifecycle_handle_shutdown(void) {
    const char *mode =
        (g_shutdown_requested == SHUTDOWN_REBOOT)  ? "reboot" :
        (g_shutdown_requested == SHUTDOWN_RESTART) ? "restart" :
        "stop";  // SHUTDOWN_STOP

    LOG_SYS(LOG_INFO, "Shutdown handler entered (%s)", mode);
    LOG_SYS(LOG_DEBUG, "shutdown mode raw=%d", g_shutdown_requested);

    static int handled = 0;
    if (handled) {
        LOG_SYS(LOG_DEBUG, "handle_shutdown already executed");
        return;
    }
    handled = 1;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // ===== REBOOT =====
    if (g_shutdown_requested == SHUTDOWN_REBOOT) {
        LOG_SYS(LOG_WARN, "Reboot requested");

        if (g_reboot_requested_by != 0) {
            LOG_SYS(LOG_INFO, "Requested by chat_id=%ld", g_reboot_requested_by);
        }

        lifecycle_log_uptime();
        graceful_shutdown();

        clock_gettime(CLOCK_MONOTONIC, &end);
        long ms = (end.tv_sec - start.tv_sec) * 1000 +
                  (end.tv_nsec - start.tv_nsec) / 1000000;

        LOG_SYS(LOG_INFO, "Reboot took %ld ms", ms);
        LOG_SYS(LOG_WARN, "Rebooting via reboot(RB_AUTOBOOT)...");
        
        logger_close();
        fflush(NULL);
        sync();

        if (reboot(RB_AUTOBOOT) != 0) {
            LOG_SYS(LOG_ERROR, "reboot() failed: errno=%d (%s)", 
                    errno, strerror(errno));

            if (env_is_ci()) {
                LOG_SYS(LOG_INFO, "Skipping systemctl reboot (CI environment)");
                return;
            }
            
            LOG_SYS(LOG_WARN, "Fallback: systemctl reboot");
            fflush(NULL);
            execl("/bin/systemctl", "systemctl", "reboot", NULL);
            LOG_SYS(LOG_ERROR, "fallback exec failed: errno=%d (%s)", 
                    errno, strerror(errno));
        }
        return;
    }

    // ===== RESTART =====
    if (g_shutdown_requested == SHUTDOWN_RESTART) {
        LOG_SYS(LOG_WARN, "Restart requested");

        if (g_reboot_requested_by != 0) {
            LOG_SYS(LOG_INFO, "Requested by chat_id=%ld", g_reboot_requested_by);
        }

        lifecycle_log_uptime();
        graceful_shutdown();

        clock_gettime(CLOCK_MONOTONIC, &end);
        long ms = (end.tv_sec - start.tv_sec) * 1000 +
                  (end.tv_nsec - start.tv_nsec) / 1000000;

        LOG_SYS(LOG_INFO, "Restart took %ld ms", ms);
        
        logger_close();
        fflush(NULL);
        sync();

        if (env_is_ci()) {
            LOG_SYS(LOG_INFO, "Skipping systemctl restart (CI environment)");
            return;
        }

        execl("/bin/systemctl", "systemctl", "restart", "tg-bot", NULL);
        LOG_SYS(LOG_ERROR, "exec restart failed: errno=%d (%s)", 
                errno, strerror(errno));
        return;
    }

    // ===== STOP (SIGTERM от systemd) =====
    lifecycle_log_uptime();
    graceful_shutdown();
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    long ms = (end.tv_sec - start.tv_sec) * 1000 +
              (end.tv_nsec - start.tv_nsec) / 1000000;

    LOG_SYS(LOG_INFO, "Shutdown took %ld ms", ms);
    LOG_SYS(LOG_INFO, "Bot stopped by signal");
    fflush(NULL);
    exit(0);
}
