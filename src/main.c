#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <signal.h>
#include <pwd.h>

#include "version.h"
#include "logger.h"
#include "config.h"
#include "cli.h"
#include "telegram.h"
#include "security.h"

// ===== uptime бота =====
time_t g_start_time;

// ===== SIGHUP reload =====
static volatile sig_atomic_t g_reload_config = 0;

// ===== signal handler =====
static void handle_sighup(int sig) {
    (void)sig;
    g_reload_config = 1;
}

// ===== DEBUG: user info =====

static void log_user_info() {
    uid_t uid = getuid();
    uid_t euid = geteuid();

    struct passwd *pw = getpwuid(uid);
    struct passwd *epw = getpwuid(euid);

    log_msg(LOG_INFO, "User UID: %d (%s)",
            uid,
            pw ? pw->pw_name : "unknown");

    log_msg(LOG_INFO, "Effective UID: %d (%s)",
            euid,
            epw ? epw->pw_name : "unknown");
}

// ===== DEBUG: cwd =====

static void log_workdir() {
    char buf[256];
    if (getcwd(buf, sizeof(buf))) {
        log_msg(LOG_INFO, "Working directory: %s", buf);
    } else {
        log_msg(LOG_WARN, "Failed to get working directory");
    }
}

// ===== DEBUG: journalctl access =====

static void check_journal_access() {
    FILE *f = popen("journalctl -n 1 --no-pager 2>&1", "r");

    if (!f) {
        log_msg(LOG_WARN, "journalctl check failed (popen)");
        return;
    }

    char line[256];

    if (fgets(line, sizeof(line), f)) {
        if (strstr(line, "not seeing messages")) {
            log_msg(LOG_WARN, "journalctl: NO ACCESS (need adm/systemd-journal)");
        } else {
            log_msg(LOG_INFO, "journalctl: access OK");
        }
    } else {
        log_msg(LOG_WARN, "journalctl: no output");
    }

    pclose(f);
}

// ===== helper: проверка записи в файл =====

static void check_logfile_access(const char *path) {
    FILE *f = fopen(path, "a");
    if (!f) {
        log_msg(LOG_WARN, "No write access to log file: %s", path);
        return;
    }
    fclose(f);
    log_msg(LOG_INFO, "Log file writable: %s", path);
}

// ===== helper: безопасное переключение логгера =====

static int try_reopen_logger(const char *path) {
    FILE *test = fopen(path, "a");
    if (!test) {
        return -1;
    }
    fclose(test);

    logger_close();

    if (logger_init(path) != 0) {
        return -1;
    }

    return 0;
}

// ===== helper: единый вывод конфига =====

static void log_config(const config_t *cfg) {
    log_msg(LOG_INFO, "===== CONFIG =====");
    log_msg(LOG_INFO, "CHAT_ID: %ld", cfg->chat_id);
    log_msg(LOG_INFO, "TOKEN_TTL: %d sec", cfg->token_ttl);
    log_msg(LOG_INFO, "POLL_TIMEOUT: %d", cfg->poll_timeout);
    log_msg(LOG_INFO, "LOG_FILE: %s", cfg->log_file);
}

// ===== main =====

int main(int argc, char *argv[]) {

    srand(time(NULL));
    g_start_time = time(NULL);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sighup;
    sigaction(SIGHUP, &sa, NULL);

    cli_args_t args;

    // ===== CLI =====
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
        printf("Error: config file not specified\n");
        printf("Use --help for usage\n");
        return 1;
    }

    // ===== bootstrap логгер =====
    const char *fallback_log = "/var/log/tg-bot.log";

    if (logger_init(fallback_log) != 0) {
        fprintf(stderr,
                "WARNING: cannot open %s, using stderr\n",
                fallback_log);
    }

    log_msg(LOG_INFO, "========================================");
    log_msg(LOG_INFO, "Starting %s v%s (%s)",
            APP_NAME, APP_VERSION, APP_CODENAME);
    log_msg(LOG_INFO, "Build: %s %s", BUILD_DATE, BUILD_TIME);
    log_msg(LOG_INFO, "Target: %s", TARGET_OS);
    log_msg(LOG_INFO, "Config: %s", args.config_path);
    log_msg(LOG_INFO, "Logger initialized (bootstrap): %s", fallback_log);

    // 🔥 DEBUG блок
    log_user_info();
    log_workdir();
    check_journal_access();
    check_logfile_access(fallback_log);

    // ===== загрузка конфига =====
    config_t cfg;

    if (config_load(args.config_path, &cfg) != 0) {
        log_msg(LOG_ERROR, "Failed to load config");
        logger_close();
        return 1;
    }

    // ===== переключение логгера =====
    if (try_reopen_logger(cfg.log_file) != 0) {
        log_msg(LOG_WARN,
                "Failed to open log file: %s (using bootstrap logger)",
                cfg.log_file);
    } else {
        log_msg(LOG_INFO, "Logger initialized: %s", cfg.log_file);
        check_logfile_access(cfg.log_file);
    }

    log_config(&cfg);

    // ===== security init =====
    security_set_allowed_chat(cfg.chat_id);
    security_set_token_ttl(cfg.token_ttl);

    // ===== Telegram init =====
    if (telegram_init(cfg.token) != 0) {
        log_msg(LOG_ERROR, "Failed to init Telegram");
        logger_close();
        return 1;
    }

    log_msg(LOG_INFO, "Telegram bot started");
    log_msg(LOG_INFO, "Entering polling loop");

    // ===== основной цикл =====
    while (1) {

        if (g_reload_config) {

            log_msg(LOG_INFO, "Reloading config (SIGHUP)...");

            config_t new_cfg;

            if (config_load(args.config_path, &new_cfg) == 0) {

                if (strcmp(cfg.log_file, new_cfg.log_file) != 0) {

                    if (try_reopen_logger(new_cfg.log_file) != 0) {
                        log_msg(LOG_WARN,
                                "Failed to reopen log file: %s",
                                new_cfg.log_file);
                    } else {
                        log_msg(LOG_INFO, "Logger reloaded: %s", new_cfg.log_file);
                    }
                }

                security_set_allowed_chat(new_cfg.chat_id);
                security_set_token_ttl(new_cfg.token_ttl);

                cfg = new_cfg;

                log_msg(LOG_INFO, "Config reloaded successfully");
                log_config(&cfg);

            } else {
                log_msg(LOG_ERROR, "Config reload failed");
            }

            g_reload_config = 0;
        }

        if (telegram_poll() != 0) {
            log_msg(LOG_WARN, "Polling error, retry in 5 sec");
            sleep(5);
            continue;
        }

        usleep(200000);
    }

    logger_close();
    return 0;
}
