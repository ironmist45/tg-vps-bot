/* tg-bot — lifecycle.c — Process lifecycle management. MIT License © 2026 ironmist45 */

/*
 * Handles:
 *   - Signal handlers (SIGHUP, SIGTERM, SIGINT, SIGUSR1)
 *   - Graceful shutdown sequence
 *   - System reboot and process restart logic
 *   - Uptime tracking and logging
 *
 * This module centralizes all shutdown/restart logic, keeping main.c clean
 * and focused on orchestration.
 */

#include "environment.h"
#include "lifecycle.h"
#include "logger.h"
#include "telegram.h"
#include "config.h"
#include "utils.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/reboot.h>
#include <time.h>
#include <stdlib.h>

// ============================================================================
// GLOBAL STATE
// ============================================================================

time_t                g_start_time          = 0;
volatile sig_atomic_t g_shutdown_requested  = 0;
volatile sig_atomic_t g_reload_config       = 0;
volatile sig_atomic_t g_rotate_log          = 0;
volatile sig_atomic_t g_signal_received     = 0;
long                  g_reboot_requested_by = 0;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

static void handle_sighup(int sig);
static void handle_sigusr1(int sig);
static void handle_sigterm(int sig);
static void graceful_shutdown(void);

// ============================================================================
// INITIALIZATION
// ============================================================================

void lifecycle_init(void) {
    g_start_time          = time(NULL);
    g_shutdown_requested  = 0;
    g_reload_config       = 0;
    g_rotate_log          = 0;
    g_signal_received     = 0;
    g_reboot_requested_by = 0;
}

void lifecycle_register_handlers(void) {
    struct sigaction sa = {0};
    sa.sa_handler = handle_sighup;
    sa.sa_flags   = SA_RESTART;
    if (sigaction(SIGHUP, &sa, NULL) != 0)
        LOG_SYS(LOG_ERROR, "sigaction SIGHUP failed: errno=%d", errno);

    struct sigaction sa_term = {0};
    sa_term.sa_handler = handle_sigterm;
    sa_term.sa_flags   = 0;
    if (sigaction(SIGTERM, &sa_term, NULL) != 0)
        LOG_SYS(LOG_ERROR, "sigaction SIGTERM failed: errno=%d", errno);
    if (sigaction(SIGINT, &sa_term, NULL) != 0)
        LOG_SYS(LOG_ERROR, "sigaction SIGINT failed: errno=%d", errno);

    struct sigaction sa_usr1 = {0};
    sa_usr1.sa_handler = handle_sigusr1;
    sa_usr1.sa_flags   = SA_RESTART;
    if (sigaction(SIGUSR1, &sa_usr1, NULL) != 0)
        LOG_SYS(LOG_ERROR, "sigaction SIGUSR1 failed: errno=%d", errno);
}

// ============================================================================
// LIFECYCLE QUERIES
// ============================================================================

int lifecycle_shutdown_requested(void) {
    return g_shutdown_requested != 0 || g_signal_received != 0;
}

int lifecycle_reload_requested(void) {
    return g_reload_config != 0;
}

int lifecycle_rotate_requested(void) {
    return g_rotate_log != 0;
}

void lifecycle_clear_rotate(void) {
    g_rotate_log = 0;
}

void lifecycle_clear_reload(void) {
    g_reload_config = 0;
    g_rotate_log    = 0;
}

void lifecycle_request_shutdown(int mode, long requested_by) {
    if (g_signal_received) return;
    g_signal_received = 1;

    g_shutdown_requested  = mode;
    g_reboot_requested_by = requested_by;

    LOG_SYS(LOG_WARN, "Shutdown requested (mode=%d) by chat_id=%ld",
            mode, requested_by);
}

static void lifecycle_log_uptime(void) {
    time_t now    = time(NULL);
    long   uptime = now - g_start_time;

    int days  = uptime / 86400;
    int hours = (uptime % 86400) / 3600;
    int mins  = (uptime % 3600) / 60;

    LOG_SYS(LOG_INFO, "Uptime: %dd %dh %dm", days, hours, mins);
}

// ============================================================================
// SIGNAL HANDLERS
//
// Only async-signal-safe operations are permitted here (POSIX).
// Logging (fprintf, snprintf) is NOT async-signal-safe — all logging
// is deferred to the main loop which checks g_signal_received /
// g_shutdown_requested after each poll iteration.
// ============================================================================

static void handle_sigusr1(int sig) {
    (void)sig;
    g_rotate_log = 1;
}

static void handle_sighup(int sig) {
    (void)sig;
    g_reload_config = 1;
}

static void handle_sigterm(int sig) {
    (void)sig;
    if (g_signal_received) return;
    g_signal_received    = 1;
    g_shutdown_requested = SHUTDOWN_STOP;
    /* Logging deferred to main loop — LOG_SYS is not async-signal-safe */
}

// ============================================================================
// GRACEFUL SHUTDOWN
// ============================================================================

static void graceful_shutdown(void) {
    /* Single-process, single-threaded — no concurrent access to done */
    static int done = 0;
    if (done) return;
    done = 1;

    /* Block signals during shutdown to avoid re-entrant handling */
    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    LOG_SYS(LOG_INFO, "Graceful shutdown: stopping services...");

    long offset = telegram_get_last_offset();
    LOG_STATE(LOG_INFO, "Saving offset: %ld", offset);

    telegram_shutdown();
    LOG_STATE(LOG_INFO, "Exiting main loop");
    usleep(200000);

    LOG_SYS(LOG_INFO, "Flushing logs...");
    fflush(NULL);

    LOG_SYS(LOG_INFO, "Shutdown sequence complete");
    fflush(NULL);
    sync();

    sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

// ============================================================================
// SHUTDOWN HANDLER
// ============================================================================

void lifecycle_handle_shutdown(void) {
    const char *mode =
        (g_shutdown_requested == SHUTDOWN_REBOOT)  ? "reboot"  :
        (g_shutdown_requested == SHUTDOWN_RESTART) ? "restart" :
        "stop";

    /* Log SIGTERM receipt here — deferred from signal handler (not async-signal-safe) */
    if (g_shutdown_requested == SHUTDOWN_STOP)
        LOG_SYS(LOG_WARN, "Received SIGTERM (PID=%d), shutting down...", getpid());

    LOG_SYS(LOG_INFO, "Shutdown handler entered (%s)", mode);
    LOG_SYS(LOG_DEBUG, "shutdown mode raw=%d", g_shutdown_requested);

    /* Single-process, single-threaded — no concurrent access to handled */
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

        if (g_reboot_requested_by != 0)
            LOG_SYS(LOG_INFO, "Requested by chat_id=%ld", g_reboot_requested_by);

        lifecycle_log_uptime();
        graceful_shutdown();

        clock_gettime(CLOCK_MONOTONIC, &end);
        LOG_SYS(LOG_INFO, "Reboot took %ld ms", elapsed_ms(start, end));
        LOG_SYS(LOG_WARN, "Rebooting via reboot(RB_AUTOBOOT)...");

        /* Close logger before reboot — no more logging after this point */
        logger_close();
        fflush(NULL);
        sync();

        if (reboot(RB_AUTOBOOT) != 0) {
            /*
             * logger is already closed — write directly to stderr so the
             * error is visible in the systemd journal.
             */
            fprintf(stderr, "reboot() failed: errno=%d (%s)\n",
                    errno, strerror(errno));

            if (env_is_ci()) {
                fprintf(stderr, "Skipping systemctl reboot (CI environment)\n");
                return;
            }

            fprintf(stderr, "Fallback: systemctl reboot\n");
            fflush(stderr);
            execl(g_cfg.systemctl_path, "systemctl", "reboot", NULL);
            fprintf(stderr, "fallback exec failed: errno=%d (%s)\n",
                    errno, strerror(errno));
        }
        return;
    }

    // ===== RESTART =====
    if (g_shutdown_requested == SHUTDOWN_RESTART) {
        LOG_SYS(LOG_WARN, "Restart requested");

        if (g_reboot_requested_by != 0)
            LOG_SYS(LOG_INFO, "Requested by chat_id=%ld", g_reboot_requested_by);

        lifecycle_log_uptime();
        graceful_shutdown();

        clock_gettime(CLOCK_MONOTONIC, &end);
        LOG_SYS(LOG_INFO, "Restart took %ld ms", elapsed_ms(start, end));

        /* Close logger before exec — no more logging after this point */
        logger_close();
        fflush(NULL);
        sync();

        if (env_is_ci()) {
            fprintf(stderr, "Skipping systemctl restart (CI environment)\n");
            return;
        }

        execl(g_cfg.systemctl_path, "systemctl", "restart", "tg-bot", NULL);
        fprintf(stderr, "exec restart failed: errno=%d (%s)\n",
                errno, strerror(errno));
        return;
    }

    // ===== STOP (SIGTERM from systemd or manual) =====
    lifecycle_log_uptime();
    graceful_shutdown();

    clock_gettime(CLOCK_MONOTONIC, &end);
    LOG_SYS(LOG_INFO, "Shutdown took %ld ms", elapsed_ms(start, end));
    LOG_SYS(LOG_INFO, "Bot stopped by signal");
    fflush(NULL);
    exit(0);
}
