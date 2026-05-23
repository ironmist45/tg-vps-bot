/**
 * tg-bot - Telegram bot for system administration
 *
 * Main entry point and orchestration layer.
 * Coordinates initialization, main event loop, and graceful shutdown.
 *
 * Business logic is delegated to:
 *   - lifecycle.c   (signal handling, reboot/restart logic)
 *   - environment.c (startup checks and diagnostics)
 *   - telegram.c    (bot API communication)
 *   - commands.c    (command processing)
 *   - security.c    (access control and token validation)
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "version.h"
#include "build_info.h"
#include "logger.h"
#include "config.h"
#include "cli.h"
#include "diagnostics.h"
#include "exec.h"
#include "telegram.h"
#include "security.h"
#include "lifecycle.h"
#include "environment.h"
#include "metrics.h"
#include "sd_notify.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

/* Maximum consecutive polling errors before exiting */
#define MAIN_MAX_CONSECUTIVE_ERRORS 5

/* Sleep duration between polling retries on error */
#define MAIN_POLL_ERROR_SLEEP_SEC   5

/* Pause between main loop iterations (microseconds) */
#define MAIN_LOOP_SLEEP_US          200000

// ============================================================================
// GLOBAL CONFIG
// ============================================================================

/* Used by exec.c, environment.c, services.c and other modules */
config_t g_cfg;

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char *argv[]) {

    // -----------------------------------------------------------------------
    // 1. Metrics reset first — before any handlers that might fire signals
    // -----------------------------------------------------------------------
    memset(&g_metrics, 0, sizeof(g_metrics));

    // -----------------------------------------------------------------------
    // 2. Lifecycle initialization (signals, timers)
    // -----------------------------------------------------------------------
    lifecycle_init();
    lifecycle_register_handlers();

    // -----------------------------------------------------------------------
    // 3. Command-line argument processing
    // -----------------------------------------------------------------------
    char config_path[CLI_CONFIG_PATH_MAX];
    int rc = cli_process(argc, argv, config_path);
    if (rc == CLI_EXIT_OK)  return 0;
    if (rc == CLI_EXIT_ERR) return 1;

    // -----------------------------------------------------------------------
    // 4. Logger initialization with fallback path
    // -----------------------------------------------------------------------
    const char *fallback_log = "/var/log/tg-bot.log";
    logger_init(fallback_log);

    // -----------------------------------------------------------------------
    // 5. Startup logging
    // -----------------------------------------------------------------------
    LOG_SYS(LOG_INFO, "==== START ====");
    LOG_SYS(LOG_INFO, "%s v%s (%s)", APP_NAME, APP_VERSION, APP_CODENAME);
    LOG_SYS(LOG_INFO, "Build #%s, commit %s, %s",
            TG_BUILD_NUMBER, TG_BUILD_COMMIT, TG_BUILD_DATE);
    fflush(NULL);

    LOG_SYS(LOG_INFO, "Process started (Main PID=%d)", getpid());

    // -----------------------------------------------------------------------
    // 6. Environment context logging
    // -----------------------------------------------------------------------
    env_log_user_info();
    env_log_workdir();

    // -----------------------------------------------------------------------
    // 7. Config loading
    // -----------------------------------------------------------------------
    if (config_load(config_path, &g_cfg) != 0) {
        LOG_CFG(LOG_ERROR, "Config load failed");
        logger_close();
        return 1;
    }

    // -----------------------------------------------------------------------
    // 8. Switch to log file from config, run environment checks
    // -----------------------------------------------------------------------
    if (logger_reopen(g_cfg.log_file) == 0) {
        LOG_CFG(LOG_INFO, "Logger switched: %s", g_cfg.log_file);
    }

    logger_set_level(g_cfg.log_level);
    LOG_SYS(LOG_INFO, "Logger level applied: %s",
            logger_level_to_string(g_cfg.log_level));

    env_check_all(g_cfg.log_file);
    config_log(&g_cfg);

    // -----------------------------------------------------------------------
    // 9. Security module initialization
    // -----------------------------------------------------------------------
    security_set_allowed_chat(g_cfg.chat_id);
    security_set_token_ttl(g_cfg.token_ttl);
    security_init();

    // -----------------------------------------------------------------------
    // 10. Telegram initialization
    // -----------------------------------------------------------------------
    if (telegram_init(g_cfg.token) != 0) {
        LOG_NET(LOG_ERROR, "Telegram init failed");
        logger_close();
        return 1;
    }

    LOG_STATE(LOG_INFO, "Bot started");

    /*
     * Notify systemd that initialization is complete and bot is ready.
     * Required when Type=notify is set in the unit file.
     * No-op if NOTIFY_SOCKET is not set.
     */
    sd_notify_ready();

    /*
     * Send startup notification to Telegram.
     * Log a warning if delivery fails — bot continues regardless.
     */
    if (telegram_send_message(g_cfg.chat_id,
            "🟢 *Bot started*\n\nReady to serve.") != 0) {
        LOG_NET(LOG_WARN, "Failed to send startup message");
    }

    LOG_STATE(LOG_INFO, "Entering main loop");

    // =======================================================================
    // 11. MAIN LOOP
    // =======================================================================
    int consecutive_errors = 0;

    while (1) {
        diag_loop_start();

        /*
         * Reset systemd watchdog timer on every iteration.
         * WatchdogSec=60 in unit file — expected every ~30s.
         * One iteration: poll (up to 25s) + sleep (200ms) — fits easily.
         * No-op if NOTIFY_SOCKET is not set.
         */
        sd_notify_watchdog();

        // -------------------------------------------------------------------
        // Check for shutdown request (from signal or command)
        // -------------------------------------------------------------------
        if (lifecycle_shutdown_requested()) {
            lifecycle_handle_shutdown();
            break;
        }

        // -------------------------------------------------------------------
        // Check for config reload request (SIGHUP)
        // -------------------------------------------------------------------
        if (lifecycle_reload_requested()) {
            config_reload(config_path, &g_cfg);
            lifecycle_clear_reload();
        }

        // -------------------------------------------------------------------
        // Log rotation request (SIGUSR1 from logrotate)
        // -------------------------------------------------------------------
        if (lifecycle_rotate_requested()) {
            LOG_SYS(LOG_INFO, "Log rotation requested (SIGUSR1)");
            if (logger_reopen(g_cfg.log_file) == 0) {
                LOG_SYS(LOG_INFO, "Log file reopened: %s", g_cfg.log_file);
            } else {
                LOG_SYS(LOG_WARN, "Failed to reopen log file: %s", g_cfg.log_file);
            }
            lifecycle_clear_rotate();
        }

        // -------------------------------------------------------------------
        // Telegram API polling
        // -------------------------------------------------------------------
        int poll_rc = telegram_poll();

        if (poll_rc != 0) {
            /* Check shutdown before sleeping on error */
            if (lifecycle_shutdown_requested()) {
                lifecycle_handle_shutdown();
                break;
            }

            g_metrics.api_calls_failed++;
            consecutive_errors++;
            LOG_NET(LOG_WARN, "Polling error (rc=%d, attempt=%d/%d)",
                    poll_rc, consecutive_errors, MAIN_MAX_CONSECUTIVE_ERRORS);

            if (consecutive_errors >= MAIN_MAX_CONSECUTIVE_ERRORS) {
                g_metrics.err_timeout++;
                LOG_SYS(LOG_ERROR,
                    "Too many consecutive polling errors (%d), shutting down",
                    MAIN_MAX_CONSECUTIVE_ERRORS);
                /*
                 * Trigger graceful shutdown to save offset and flush state —
                 * same path as SIGTERM so no data is lost on network failure.
                 */
                g_shutdown_requested = SHUTDOWN_STOP;
                lifecycle_handle_shutdown();
                break;
            }

            sleep(MAIN_POLL_ERROR_SLEEP_SEC);
        } else {
            g_metrics.api_calls_total++;
            g_metrics.poll_count++;
            consecutive_errors = 0;
        }

        /*
         * Record loop timing before sleep — diag measures useful work only,
         * not the inter-iteration pause.
         */
        diag_loop_end();
        usleep(MAIN_LOOP_SLEEP_US);
    }

    // -----------------------------------------------------------------------
    // 12. Shutdown
    // -----------------------------------------------------------------------
    char metrics_buf[512];
    metrics_format_log(metrics_buf, sizeof(metrics_buf));
    LOG_SYS(LOG_INFO, "%s", metrics_buf);

    logger_close();
    return 0;
}
