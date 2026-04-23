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

#include "version.h"
#include "logger.h"
#include "config.h"
#include "cli.h"
#include "exec.h"
#include "telegram.h"
#include "security.h"
#include "lifecycle.h"
#include "environment.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

// ===== Прототипы вспомогательных функций =====
static int try_reopen_logger(const char *path);
static void log_config(const config_t *cfg);

// ===== MAIN =====

int main(int argc, char *argv[]) {
    // -----------------------------------------------------------
    // 1. Инициализация жизненного цикла (сигналы, таймеры)
    // -----------------------------------------------------------
    lifecycle_init();
    lifecycle_register_handlers();

    // -----------------------------------------------------------
    // 2. Обработка аргументов командной строки
    // -----------------------------------------------------------
    char config_path[256];
    int rc = cli_process(argc, argv, config_path);
    if (rc != 0) {
        // Help/version printed or error — exit cleanly
        return (rc == -1 && (argc == 2)) ? 0 : 1;
    }

    // -----------------------------------------------------------
    // 3. Инициализация логгера с fallback-путём
    // -----------------------------------------------------------
    const char *fallback_log = "/var/log/tg-bot.log";
    logger_init(fallback_log);

    // -----------------------------------------------------------
    // 4. Стартовое логирование
    // -----------------------------------------------------------
    LOG_SYS(LOG_INFO, "==== START ====");
    LOG_SYS(LOG_INFO, "%s v%s (%s)", APP_NAME, APP_VERSION, APP_CODENAME);
    fflush(NULL);
    
    LOG_SYS(LOG_INFO, "Process started (PID=%d)", getpid());
    
    // -----------------------------------------------------------
    // 5. Логирование контекста и проверки окружения
    // -----------------------------------------------------------
    env_log_user_info();
    env_log_workdir();
    env_check_all(fallback_log);

    // -----------------------------------------------------------
    // 6. Загрузка конфигурации
    // -----------------------------------------------------------
    config_t cfg;
    if (config_load(config_path, &cfg) != 0) {
        LOG_CFG(LOG_ERROR, "Config load failed");
        logger_close();
        return 1;
    }

    // -----------------------------------------------------------
    // 7. Переключение на лог-файл из конфига (если указан)
    // -----------------------------------------------------------
    if (logger_reopen(cfg.log_file) == 0) {
        LOG_CFG(LOG_INFO, "Logger switched: %s", cfg.log_file);
    }

    logger_set_level(cfg.log_level);
    LOG_SYS(LOG_INFO, "Logger level applied: %s", 
            logger_level_to_string(cfg.log_level));
    
    log_config(&cfg);

    // -----------------------------------------------------------
    // 8. Инициализация модулей безопасности
    // -----------------------------------------------------------
    security_set_allowed_chat(cfg.chat_id);
    security_set_token_ttl(cfg.token_ttl);
    security_init();

    // -----------------------------------------------------------
    // 9. Инициализация Telegram
    // -----------------------------------------------------------
    if (telegram_init(cfg.token) != 0) {
        LOG_NET(LOG_ERROR, "Telegram init failed");
        logger_close();
        return 1;
    }

    LOG_STATE(LOG_INFO, "Bot started");
    LOG_STATE(LOG_INFO, "Entering main loop");

    // ===========================================================
    // 10. ГЛАВНЫЙ ЦИКЛ
    // ===========================================================
    int consecutive_errors = 0;
    const int max_consecutive_errors = 5;
    
    while (1) {
        // -------------------------------------------------------
        // Проверка запроса на shutdown (от сигнала или команды)
        // -------------------------------------------------------
        if (lifecycle_shutdown_requested()) {
            lifecycle_handle_shutdown();
            break;
        }

        // -------------------------------------------------------
        // Проверка запроса на перезагрузку конфигурации (SIGHUP)
        // -------------------------------------------------------
        if (lifecycle_reload_requested()) {
            LOG_STATE(LOG_INFO, "Reload config");

            config_t new_cfg;
            if (config_load(config_path, &new_cfg) == 0) {
                // Если изменился путь к лог-файлу — переоткрываем
                if (strcmp(cfg.log_file, new_cfg.log_file) != 0) {
                    logger_reopen(new_cfg.log_file);
                }

                logger_set_level(new_cfg.log_level);
                security_set_allowed_chat(new_cfg.chat_id);
                security_set_token_ttl(new_cfg.token_ttl);

                cfg = new_cfg;
                log_config(&cfg);
            }

            lifecycle_clear_reload();
        }

        // -------------------------------------------------------
        // Polling Telegram API
        // -------------------------------------------------------
        int poll_rc = telegram_poll();
        if (poll_rc != 0) {
            // 🔥 CHECK SHUTDOWN FLAG IMMEDIATELY!
            if (lifecycle_shutdown_requested()) {
                lifecycle_handle_shutdown();
                break;
            }
            
            consecutive_errors++;
            LOG_NET(LOG_WARN, "Polling error (rc=%d, attempt=%d/%d)",
                    poll_rc, consecutive_errors, max_consecutive_errors);

            if (consecutive_errors >= max_consecutive_errors) {
                LOG_SYS(LOG_ERROR, "Too many consecutive polling errors, exiting");
                break;
            }
            sleep(5);
        } else {
            consecutive_errors = 0;
        }
                    
        usleep(200000);  // небольшая пауза между циклами
    }

    // -----------------------------------------------------------
    // 11. Завершение работы
    // -----------------------------------------------------------
    logger_close();
    return 0;
}

// =====================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// =====================================================================

/**
 * Переоткрытие лог-файла (например, после ротации или смены пути в конфиге)
 * 
 * @param path  путь к новому лог-файлу
 * @return      0 при успехе, -1 при ошибке
 */
static int try_reopen_logger(const char *path) {
    // Проверяем, что файл доступен для записи
    FILE *f = fopen(path, "a");
    if (!f) return -1;
    fclose(f);

    // Сбрасываем буферы перед закрытием старого логгера
    fflush(NULL);
    logger_close();
    
    return logger_init(path);
}

/**
 * Логирование текущей конфигурации (для отладки)
 * 
 * @param cfg  указатель на структуру конфигурации
 */
static void log_config(const config_t *cfg) {
    LOG_CFG(LOG_INFO, "===== CONFIG =====");
    LOG_CFG(LOG_INFO, "CHAT_ID: %ld", cfg->chat_id);
    LOG_CFG(LOG_INFO, "TOKEN_TTL: %d", cfg->token_ttl);
    LOG_CFG(LOG_INFO, "POLL_TIMEOUT: %d", cfg->poll_timeout);
    LOG_CFG(LOG_INFO, "LOG_FILE: %s", cfg->log_file);
    LOG_CFG(LOG_INFO, "LOG_LEVEL: %s", logger_level_to_string(cfg->log_level));
}
