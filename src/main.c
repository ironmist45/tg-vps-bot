#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <signal.h>
#include <pwd.h>
#include <sys/types.h>

#include <sys/reboot.h>
#include <errno.h>

#include "version.h"
#include "logger.h"
#include "config.h"
#include "cli.h"
#include "telegram.h"
#include "security.h"

// ===== uptime =====
time_t g_start_time;

// 👉 кто запросил reboot (бонус)
long g_reboot_requested_by = 0;

// ===== signals =====
static volatile sig_atomic_t g_reload_config = 0;
volatile sig_atomic_t g_shutdown_requested = 0; // 0=none,1=restart,2=reboot

// ===== STATE =====
static volatile int g_running = 1;

// 🔥 защита от повторных сигналов
static volatile sig_atomic_t g_signal_received = 0;

// ===== SIGNAL HANDLERS =====

static void handle_sighup(int sig) {
    (void)sig;
    g_reload_config = 1;
}

static void handle_sigterm(int sig) {
    (void)sig;

    // 🔥 защита от спама сигналами
    if (g_signal_received) return;
    g_signal_received = 1;

    log_msg(LOG_WARN, "Received SIGTERM, shutting down...");
    g_shutdown_requested = 1;
    g_running = 0;
}

// ===== UTILS =====

static void log_uptime() {
    time_t now = time(NULL);
    long uptime = now - g_start_time;

    int days = uptime / 86400;
    int hours = (uptime % 86400) / 3600;
    int mins = (uptime % 3600) / 60;

    log_msg(LOG_INFO,
        "Uptime: %dd %dh %dm",
        days, hours, mins);
}

// ===== GRACEFUL SHUTDOWN =====

static void graceful_shutdown() {

    static int done = 0;
    if (done) return;
    done = 1;

    log_msg(LOG_INFO, "Graceful shutdown: stopping services...");

    // 1. остановить telegram
    telegram_shutdown();

    // 2. немного времени на завершение сетевых операций
    usleep(200000); // 200 ms

    // 3. flush логов
    log_msg(LOG_INFO, "Flushing logs...");
    fflush(NULL);
    
    // 4. sync файловой системы
    log_msg(LOG_INFO, "Shutdown sequence complete");
    fflush(NULL);
    
    logger_close();
    
    sync();
}

// ===== SHUTDOWN HANDLER =====

static void handle_shutdown() {

    static int handled = 0;
    
    if (handled) {
        log_msg(LOG_DEBUG, "handle_shutdown already executed");
        return;
    }
    
    handled = 1;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // ===== REBOOT =====
    if (g_shutdown_requested == 2) {

        log_msg(LOG_WARN, "Reboot requested");

        if (g_reboot_requested_by != 0) {
            log_msg(LOG_INFO,
                "Requested by chat_id=%ld",
                g_reboot_requested_by);
        }

        log_uptime();

        graceful_shutdown();

        clock_gettime(CLOCK_MONOTONIC, &end);

        long ms =
            (end.tv_sec - start.tv_sec) * 1000 +
            (end.tv_nsec - start.tv_nsec) / 1000000;

        log_msg(LOG_INFO, "Shutdown took %ld ms", ms);

        log_msg(LOG_WARN, "Rebooting via reboot(RB_AUTOBOOT)...");

        if (reboot(RB_AUTOBOOT) != 0) {
            log_msg(LOG_ERROR,
                    "reboot() failed: errno=%d (%s)",
                    errno, strerror(errno));

            log_msg(LOG_WARN, "Fallback: systemctl reboot");

            fflush(NULL); // flush всех буферов

            execl("/bin/systemctl", "systemctl", "reboot", NULL);

            log_msg(LOG_ERROR,
                    "fallback exec failed: errno=%d (%s)",
                    errno, strerror(errno));
        }

        return;
    }

    // ===== RESTART =====
    if (g_shutdown_requested == 1) {

        log_msg(LOG_WARN, "Restart requested");

        if (g_reboot_requested_by != 0) {
            log_msg(LOG_INFO,
                "Requested by chat_id=%ld",
                g_reboot_requested_by);
        }

        log_uptime();

        graceful_shutdown();

        clock_gettime(CLOCK_MONOTONIC, &end);

        long ms =
            (end.tv_sec - start.tv_sec) * 1000 +
            (end.tv_nsec - start.tv_nsec) / 1000000;

        log_msg(LOG_INFO, "Shutdown took %ld ms", ms);
        fflush(NULL);

        execl("/bin/systemctl",
              "systemctl",
              "restart",
              "tg-bot",
              NULL);

        log_msg(LOG_ERROR,
                "exec restart failed: errno=%d (%s)",
                errno, strerror(errno));
    }
}

// ===== DEBUG =====

static void log_user_info() {
    uid_t uid = getuid();
    uid_t euid = geteuid();

    struct passwd *pw = getpwuid(uid);
    struct passwd *epw = getpwuid(euid);

    log_msg(LOG_INFO, "User UID: %d (%s)",
            uid, pw ? pw->pw_name : "unknown");

    log_msg(LOG_INFO, "Effective UID: %d (%s)",
            euid, epw ? epw->pw_name : "unknown");
}

static void log_workdir() {
    char buf[256];
    if (getcwd(buf, sizeof(buf))) {
        log_msg(LOG_INFO, "Working directory: %s", buf);
    }
}

static void check_journal_access() {
    FILE *f = popen("journalctl -n 1 --no-pager 2>&1", "r");

    if (!f) return;

    char line[256];

    if (fgets(line, sizeof(line), f)) {
        if (strstr(line, "not seeing messages")) {
            log_msg(LOG_WARN, "journalctl: NO ACCESS");
        } else {
            log_msg(LOG_INFO, "journalctl: access OK");
        }
    }

    pclose(f);
}

static void check_logfile_access(const char *path) {
    FILE *f = fopen(path, "a");
    if (!f) {
        log_msg(LOG_WARN, "No write access: %s", path);
        return;
    }
    fclose(f);
}

// ===== LOGGER SWITCH =====

static int try_reopen_logger(const char *path) {
    FILE *f = fopen(path, "a");
    if (!f) return -1;
    fclose(f);

    // 🔥 ВАЖНО: сбросить буферы перед закрытием
    fflush(NULL);

    logger_close();
    return logger_init(path);
}

// ===== CONFIG LOG =====

static void log_config(const config_t *cfg) {
    log_msg(LOG_INFO, "===== CONFIG =====");
    log_msg(LOG_INFO, "CHAT_ID: %ld", cfg->chat_id);
    log_msg(LOG_INFO, "TOKEN_TTL: %d", cfg->token_ttl);
    log_msg(LOG_INFO, "POLL_TIMEOUT: %d", cfg->poll_timeout);
    log_msg(LOG_INFO, "LOG_FILE: %s", cfg->log_file);
}

// ===== MAIN =====

int main(int argc, char *argv[]) {

    srand(time(NULL));
    g_start_time = time(NULL);

    log_msg(LOG_INFO, "Process started (PID=%d)", getpid());

    struct sigaction sa = {0};
    sa.sa_handler = handle_sighup;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);

    struct sigaction sa_term = {0};
    sa_term.sa_handler = handle_sigterm;
    sa_term.sa_flags = SA_RESTART;
    sigaction(SIGTERM, &sa_term, NULL);
    sigaction(SIGINT, &sa_term, NULL);

    cli_args_t args;

    if (cli_parse(argc, argv, &args) != 0) {
        printf("Invalid arguments\n");
        return 1;
    }

    if (args.show_help) {
        cli_print_help();
        return 0;
    }

    if (args.show_version) {
        cli_print_version();
        return 0;
    }

    if (args.config_path[0] == '\0') {
        printf("Config not specified\n");
        return 1;
    }

    const char *fallback_log = "/var/log/tg-bot.log";

    logger_init(fallback_log);

    log_msg(LOG_INFO, "==== START ====");
    log_msg(LOG_INFO, "%s v%s (%s)",
            APP_NAME, APP_VERSION, APP_CODENAME);
    fflush(NULL); // 🔥 flush стартовой шапки
    log_user_info();
    log_workdir();
    check_journal_access();
    check_logfile_access(fallback_log);

    config_t cfg;

    if (config_load(args.config_path, &cfg) != 0) {
        log_msg(LOG_ERROR, "Config load failed");
        return 1;
    }

    logger_set_level(cfg.log_level);

    if (try_reopen_logger(cfg.log_file) == 0) {
        log_msg(LOG_INFO, "Logger switched: %s", cfg.log_file);
    }

    logger_set_level(cfg.log_level);

    log_config(&cfg);

    security_set_allowed_chat(cfg.chat_id);
    security_set_token_ttl(cfg.token_ttl);
    security_init();

    if (telegram_init(cfg.token) != 0) {
        log_msg(LOG_ERROR, "Telegram init failed");
        return 1;
    }

    log_msg(LOG_INFO, "Bot started");
    log_msg(LOG_INFO, "Entering main loop");

    // ===== LOOP =====

    while (g_running) {

    if (g_shutdown_requested) {
        handle_shutdown();
        break;
    }

    if (g_reload_config) {

        log_msg(LOG_INFO, "Reload config");

        config_t new_cfg;

        if (config_load(args.config_path, &new_cfg) == 0) {

            if (strcmp(cfg.log_file, new_cfg.log_file) != 0) {
                try_reopen_logger(new_cfg.log_file);
            }

            logger_set_level(new_cfg.log_level);

            security_set_allowed_chat(new_cfg.chat_id);
            security_set_token_ttl(new_cfg.token_ttl);

            cfg = new_cfg;
            log_config(&cfg);
        }

        g_reload_config = 0;
    }

    int poll_rc = telegram_poll();

    if (poll_rc != 0) {
        log_msg(LOG_WARN, "Polling error (rc=%d)", poll_rc);
        sleep(5);
    }

    if (!g_running) {
        log_msg(LOG_DEBUG, "Loop exit requested");
        break;
    }

    usleep(200000);
}

    log_msg(LOG_INFO, "Bot stopped");
    logger_close();
    return 0;
}
