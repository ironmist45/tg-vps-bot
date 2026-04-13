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

// ===== signals =====
static volatile sig_atomic_t g_reload_config = 0;
volatile sig_atomic_t g_shutdown_requested = 0; // 0=none,1=restart,2=reboot

// ===== STATE =====
static volatile int g_running = 1;

// ===== SIGNAL HANDLERS =====

static void handle_sighup(int sig) {
    (void)sig;
    g_reload_config = 1;
}

static void handle_sigterm(int sig) {
    (void)sig;
    log_msg(LOG_WARN, "Received SIGTERM, shutting down...");
    g_shutdown_requested = 1;
    g_running = 0;
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
    logger_close();

    // 4. sync файловой системы
    sync();

    log_msg(LOG_INFO, "Shutdown sequence complete");
}

// ===== SHUTDOWN HANDLER =====

static void handle_shutdown() {

    static int handled = 0;
    if (handled) return;
    handled = 1;

    // ===== REBOOT =====
    if (g_shutdown_requested == 2) {

        log_msg(LOG_WARN, "Reboot requested");

        graceful_shutdown();

        log_msg(LOG_WARN, "Rebooting via reboot(RB_AUTOBOOT)...");

        if (reboot(RB_AUTOBOOT) != 0) {
            log_msg(LOG_ERROR,
                    "reboot() failed: errno=%d (%s)",
                    errno, strerror(errno));

            log_msg(LOG_WARN, "Fallback: systemctl reboot");

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

        graceful_shutdown();

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

    struct sigaction sa = {0};
    sa.sa_handler = handle_sighup;
    sigaction(SIGHUP, &sa, NULL);

    struct sigaction sa_term = {0};
    sa_term.sa_handler = handle_sigterm;
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
    log_user_info();
    log_workdir();
    check_journal_access();
    check_logfile_access(fallback_log);

    config_t cfg;

    if (config_load(args.config_path, &cfg) != 0) {
        log_msg(LOG_ERROR, "Config load failed");
        return 1;
    }

    if (try_reopen_logger(cfg.log_file) == 0) {
        log_msg(LOG_INFO, "Logger switched: %s", cfg.log_file);
    }

    log_config(&cfg);

    security_set_allowed_chat(cfg.chat_id);
    security_set_token_ttl(cfg.token_ttl);
    security_init();

    if (telegram_init(cfg.token) != 0) {
        log_msg(LOG_ERROR, "Telegram init failed");
        return 1;
    }

    log_msg(LOG_INFO, "Bot started");

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

                security_set_allowed_chat(new_cfg.chat_id);
                security_set_token_ttl(new_cfg.token_ttl);

                cfg = new_cfg;
                log_config(&cfg);
            }

            g_reload_config = 0;
        }

        if (telegram_poll() != 0) {
            log_msg(LOG_WARN, "Polling error");
            sleep(5);
        }

        usleep(200000);
    }

    log_msg(LOG_INFO, "Bot stopped");
    logger_close();
    return 0;
}
