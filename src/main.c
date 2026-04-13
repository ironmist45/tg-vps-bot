#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <signal.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "version.h"
#include "logger.h"
#include "config.h"
#include "cli.h"
#include "telegram.h"
#include "security.h"

// ===== uptime бота =====
time_t g_start_time;

// ===== сигналы =====
static volatile sig_atomic_t g_reload_config = 0;
volatile sig_atomic_t g_shutdown_requested = 0; // 🔥 0=none,1=restart,2=reboot

// ===== signal handler =====
static void handle_sighup(int sig) {
    (void)sig;
    g_reload_config = 1;
}

// ===== EXEC helper (без system) =====

static void exec_command(const char *path, char *const argv[]) {
    pid_t pid = fork();

    if (pid == 0) {
        execv(path, argv);
        _exit(127); // если exec не сработал
    }
}

// ===== shutdown handler =====

static void handle_shutdown() {

    if (g_shutdown_requested == 1) {
        log_msg(LOG_WARN, "Restarting bot via systemd...");

        char *args[] = {
            "/bin/systemctl",
            "restart",
            "tg-bot",
            NULL
        };

        exec_command("/bin/systemctl", args);
    }

    if (g_shutdown_requested == 2) {
        log_msg(LOG_WARN, "Rebooting system...");

        char *args[] = {
            "/sbin/reboot",
            NULL
        };

        exec_command("/sbin/reboot", args);
    }
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
            log_msg(LOG_WARN, "journalctl: NO ACCESS");
        } else {
            log_msg(LOG_INFO, "journalctl: access OK");
        }
    } else {
        log_msg(LOG_WARN, "journalctl: no output");
    }

    pclose(f);
}

// ===== helper: проверка записи =====

static void check_logfile_access(const char *path) {
    FILE *f = fopen(path, "a");
    if (!f) {
        log_msg(LOG_WARN, "No write access: %s", path);
        return;
    }
    fclose(f);
    log_msg(LOG_INFO, "Log writable: %s", path);
}

// ===== logger switch =====

static int try_reopen_logger(const char *path) {
    FILE *test = fopen(path, "a");
    if (!test) return -1;
    fclose(test);

    logger_close();

    if (logger_init(path) != 0)
        return -1;

    return 0;
}

// ===== config dump =====

static void log_config(const config_t *cfg) {
    log_msg(LOG_INFO, "===== CONFIG =====");
    log_msg(LOG_INFO, "CHAT_ID: %ld", cfg->chat_id);
    log_msg(LOG_INFO, "TOKEN_TTL: %d", cfg->token_ttl);
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
        printf("Config not specified\n");
        return 1;
    }

    // ===== bootstrap logger =====
    const char *fallback_log = "/var/log/tg-bot.log";

    if (logger_init(fallback_log) != 0) {
        fprintf(stderr, "Cannot open %s\n", fallback_log);
    }

    log_msg(LOG_INFO, "==== START ====");
    log_user_info();
    log_workdir();
    check_journal_access();
    check_logfile_access(fallback_log);

    // ===== config =====
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

    if (telegram_init(cfg.token) != 0) {
        log_msg(LOG_ERROR, "Telegram init failed");
        return 1;
    }

    log_msg(LOG_INFO, "Bot started");

    // ===== LOOP =====
    while (1) {

        // 🔥 shutdown handler
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
