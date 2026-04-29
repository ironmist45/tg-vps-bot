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
 * Copyright (c) 2026 ironmist45
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

#include "build_info.h"
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

// ===== Глобальный конфиг =====
config_t g_cfg;

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
    LOG_SYS(LOG_INFO, "Build: commit %s, %s", TG_BUILD_COMMIT, TG_BUILD_DATE);
    LOG_SYS(LOG_INFO, "Process started (PID=%d)", getpid());
    fflush(NULL);
    
    // -----------------------------------------------------------
    // 5. Логирование контекста и проверки окружения
    // -----------------------------------------------------------
    env_log_user_info();
    env_log_workdir();
    env_check_all(fallback_log);

    // -----------------------------------------------------------
    // 6. Загрузка конфигурации
    // -----------------------------------------------------------
    if (config_load(config_path, &g_cfg) != 0) {
        LOG_CFG(LOG_ERROR, "Config load failed");
        logger_close();
        return 1;
    }

    // -----------------------------------------------------------
    // 7. Переключение на лог-файл из конфига (если указан)
    // -----------------------------------------------------------
    if (logger_reopen(g_cfg.log_file) == 0) {
        LOG_CFG(LOG_INFO, "Logger switched: %s", g_cfg.log_file);
    }

    logger_set_level(g_cfg.log_level);
    LOG_SYS(LOG_INFO, "Logger level applied: %s", 
            logger_level_to_string(g_cfg.log_level));
    
    config_log(&g_cfg);

    // -----------------------------------------------------------
    // 8. Инициализация модулей безопасности
    // -----------------------------------------------------------
    security_set_allowed_chat(g_cfg.chat_id);
    security_set_token_ttl(g_cfg.token_ttl);
    security_init();

    // -----------------------------------------------------------
    // 9. Инициализация Telegram
    // -----------------------------------------------------------
    if (telegram_init(g_cfg.token) != 0) {
        LOG_NET(LOG_ERROR, "Telegram init failed");
        logger_close();
        return 1;
    }

    LOG_STATE(LOG_INFO, "Bot started");
    LOG_STATE(LOG_INFO, "Entering main loop");
    // Отправка стартового сообщения в чат
    telegram_send_message(g_cfg.chat_id, "🟢 *Bot started*\n\nReady to serve.");

    // ===========================================================
    // 10. ГЛАВНЫЙ ЦИКЛ
    // ===========================================================
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
            config_reload(config_path, &g_cfg);
            lifecycle_clear_reload();
        }

        // -------------------------------------------------------
        // Polling Telegram API
        // -------------------------------------------------------
        if (telegram_poll_safe(5) != 0) {
            LOG_STATE(LOG_INFO, "Exiting main loop (shutdown requested)");
            if (lifecycle_shutdown_requested()) {
                lifecycle_handle_shutdown();
            }
            break;
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
