/**
 * tg-bot - Telegram bot for system administration
 * telegram_poll.c - Long polling implementation with fork isolation
 *
 * Архитектура изоляции libcurl:
 *   HTTP-запрос к Telegram API выполняется в дочернем процессе.
 *   Это защищает родителя от зависания libcurl (который не всегда
 *   корректно реагирует на сигналы) и позволяет делать graceful
 *   shutdown по SIGTERM без ожидания curl.
 *
 *   Схема взаимодействия:
 *     parent                        child
 *       |-- fork() ----------------> |
 *       |                            |-- telegram_http_request()
 *       |-- poll(pipe, POLLIN) <-----|-- write(pipe, result)
 *       |                            |-- _exit(0)
 *       |-- read(pipe) --------------|
 *       |-- waitpid(blocking) -------|  (child уже мёртв)
 *
 *   Порядок: сначала читаем данные из pipe, затем waitpid().
 *   Это гарантирует нулевое окно зомби-состояния с точки зрения
 *   наблюдателя: к моменту waitpid() дочерний уже завершился
 *   (EOF на pipe = дочерний закрыл свой конец = _exit вызван).
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "telegram.h"
#include "telegram_poll.h"
#include "telegram_http.h"
#include "telegram_parser.h"
#include "telegram_offset.h"
#include "lifecycle.h"
#include "logger.h"
#include "commands.h"
#include "security.h"
#include "metrics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>
#include <time.h>

#define URL_MAX      1024
#define RESP_MAX     8192

/*
 * Таймаут ожидания дочернего процесса в миллисекундах.
 * curl настроен на CURLOPT_TIMEOUT=35 сек (telegram_http.c),
 * поэтому берём с небольшим запасом.
 */
#define CHILD_WAIT_TIMEOUT_MS  38000

static long           g_last_processed_update = 0;
static int            g_offset_counter        = 0;
static unsigned short g_poll_cycle            = 0;
static unsigned short g_current_poll_id       = 0;

// ============================================================================
// ИНИЦИАЛИЗАЦИЯ
// ============================================================================

void telegram_poll_init(void) {
    LOG_NET(LOG_INFO, "Telegram poll module initialized");
}

unsigned short telegram_get_poll_id(void) {
    return g_current_poll_id;
}

// ============================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================================

/*
 * kill_and_reap - безопасно убить дочерний процесс и забрать его статус.
 *
 * Используется в path'ах завершения по таймауту или shutdown.
 * После SIGKILL дочерний не может не умереть, поэтому waitpid()
 * здесь блокирующий — он вернётся очень быстро.
 */
static void kill_and_reap(pid_t pid, unsigned short poll_id) {
    if (kill(pid, SIGKILL) == 0) {
        LOG_NET(LOG_DEBUG, "poll=%04x SIGKILL sent to child PID=%d", poll_id, pid);
    }
    /* Блокирующий waitpid: после SIGKILL ждать долго не придётся */
    if (waitpid(pid, NULL, 0) == -1) {
        LOG_NET(LOG_WARN, "poll=%04x waitpid after kill failed: errno=%d", poll_id, errno);
    }
}

/*
 * read_pipe_data - читаем данные из pipe в chunk_data.
 *
 * Протокол (пишет дочерний):
 *   "<rc>\n<data_len>\n<data_bytes>"
 *
 * Возвращает http_rc через out-параметр.
 * chunk_data всегда null-terminated после вызова.
 */
static void read_pipe_data(int fd,
                           char *chunk_data, size_t chunk_size,
                           int *out_http_rc,
                           unsigned short poll_id)
{
    *out_http_rc = -1;

    FILE *pipe_read = fdopen(fd, "r");
    if (!pipe_read) {
        LOG_NET(LOG_ERROR, "poll=%04x fdopen(pipe_read) failed: errno=%d",
                poll_id, errno);
        return;
    }

    int    rc       = -1;
    size_t data_len = 0;

    if (fscanf(pipe_read, "%d\n%zu\n", &rc, &data_len) != 2) {
        LOG_NET(LOG_WARN, "poll=%04x failed to parse pipe header", poll_id);
        fclose(pipe_read);
        return;
    }

    *out_http_rc = rc;

    if (data_len == 0) {
        LOG_NET(LOG_DEBUG, "poll=%04x pipe: rc=%d, data_len=0", poll_id, rc);
        fclose(pipe_read);
        return;
    }

    /* Защита от выхода за границу буфера */
    if (data_len >= chunk_size) {
        LOG_NET(LOG_WARN,
                "poll=%04x pipe data_len=%zu exceeds buffer (%zu), clamping",
                poll_id, data_len, chunk_size);
        data_len = chunk_size - 1;
    }

    size_t bytes_read = fread(chunk_data, 1, data_len, pipe_read);
    chunk_data[bytes_read] = '\0';

    LOG_NET(LOG_DEBUG,
            "poll=%04x pipe: rc=%d data_len=%zu read=%zu first100=%.100s",
            poll_id, rc, data_len, bytes_read, chunk_data);

    fclose(pipe_read);
}

// ============================================================================
// ОЖИДАНИЕ ДОЧЕРНЕГО ПРОЦЕССА (КЛЮЧЕВОЙ БЛОК)
// ============================================================================

/*
 * wait_for_child - ждём завершения дочернего процесса через poll() на pipe.
 *
 * Почему poll() на pipe, а не WNOHANG-цикл:
 *
 *   Старый подход: waitpid(WNOHANG) + poll(NULL, 0, 100ms)
 *     - busy-wait с паузой 100мс
 *     - окно зомби до 100мс (видно в ps)
 *     - лишние итерации цикла
 *
 *   Новый подход: poll(pipefd[0], POLLIN, timeout)
 *     - pipe становится readable ровно в момент, когда дочерний
 *       закрывает свой конец (fclose(pipe_write) → _exit(0))
 *     - мы просыпаемся мгновенно, без polling
 *     - после возврата из poll() дочерний уже завершился или
 *       завершается прямо сейчас → waitpid(blocking) вернётся
 *       немедленно, без зомби-окна
 *     - shutdown проверяется при каждом EINTR — реакция мгновенная
 *
 * Возвращает:
 *   0  — дочерний завершился нормально, данные в pipe готовы
 *  -1  — таймаут или shutdown (дочерний убит, pipe закрыт)
 */
static int wait_for_child(pid_t pid, int pipe_read_fd,
                          unsigned short poll_id)
{
    struct pollfd pfd = {
        .fd     = pipe_read_fd,
        .events = POLLIN
    };

    int remaining_ms = CHILD_WAIT_TIMEOUT_MS;

    while (remaining_ms > 0) {

        /* --- Проверка shutdown до вызова poll() --- */
        if (g_signal_received != 0 || g_shutdown_requested != 0) {
            LOG_NET(LOG_WARN,
                    "poll=%04x shutdown requested, killing child PID=%d "
                    "(shutdown=%d signal=%d)",
                    poll_id, pid,
                    g_shutdown_requested, g_signal_received);
            kill_and_reap(pid, poll_id);
            return -1;
        }

        int rc = poll(&pfd, 1, 200);   /* просыпаемся каждые 200мс для проверки shutdown */

        if (rc > 0) {
            /*
             * Pipe readable (POLLIN) или дочерний закрыл свой конец (POLLHUP).
             * Оба варианта означают: дочерний завершился или завершается.
             * Данные готовы к чтению.
             */
            if (pfd.revents & (POLLIN | POLLHUP)) {
                LOG_NET(LOG_DEBUG,
                        "poll=%04x pipe ready (revents=0x%x), child PID=%d done",
                        poll_id, pfd.revents, pid);
                return 0;
            }
            /* POLLERR / POLLNVAL — нештатная ситуация */
            LOG_NET(LOG_WARN,
                    "poll=%04x pipe error (revents=0x%x)",
                    poll_id, pfd.revents);
            kill_and_reap(pid, poll_id);
            return -1;
        }

        if (rc == 0) {
            /* Истёк квант 200мс, уменьшаем оставшееся время */
            remaining_ms -= 200;
            continue;
        }

        /* rc < 0 */
        if (errno == EINTR) {
            /*
             * Прерваны сигналом — проверим shutdown в начале
             * следующей итерации. Время не уменьшаем: это не таймаут.
             */
            LOG_NET(LOG_DEBUG, "poll=%04x poll() interrupted by signal", poll_id);
            continue;
        }

        /* Неожиданная ошибка poll() */
        LOG_NET(LOG_ERROR, "poll=%04x poll() error: errno=%d", poll_id, errno);
        kill_and_reap(pid, poll_id);
        return -1;
    }

    /* Общий таймаут исчерпан */
    LOG_NET(LOG_ERROR,
            "poll=%04x child PID=%d timed out after %d ms, killing",
            poll_id, pid, CHILD_WAIT_TIMEOUT_MS);
    kill_and_reap(pid, poll_id);
    return -1;
}

// ============================================================================
// ОБРАБОТКА ОБНОВЛЕНИЙ
// ============================================================================

static void process_updates(const char *chunk_data, size_t data_len,
                            unsigned short poll_id,
                            long *last_update_id)
{
    telegram_update_t *updates = NULL;
    int count = 0;

    if (telegram_parse_updates(chunk_data, data_len, poll_id,
                               &updates, &count) != 0) {
        return;
    }

    for (int i = 0; i < count; i++) {
        telegram_update_t *u = &updates[i];

        if (u->update_id <= g_last_processed_update)
            continue;

        g_last_processed_update = u->update_id;
        *last_update_id = u->update_id + 1;
        telegram_offset_save(*last_update_id);

        if (++g_offset_counter >= 5) {
            LOG_NET(LOG_DEBUG, "saving offset: %ld", *last_update_id);
            telegram_offset_save(*last_update_id);
            g_offset_counter = 0;
        }

        LOG_NET(LOG_INFO,
                "poll=%04x req=%04x upd=%ld incoming: "
                "chat_id=%ld len=%zu text=%.64s",
                g_current_poll_id, u->req_id, u->update_id,
                u->chat_id, strlen(u->text), u->text);

        if (!security_is_allowed_chat(u->chat_id)) {
            LOG_NET(LOG_WARN,
                    "poll=%04x req=%04x ACCESS CHECK: "
                    "chat_id=%ld cmd=%s result=DENIED",
                    g_current_poll_id, u->req_id,
                    u->chat_id, u->text);
            continue;
        }

        if (security_validate_text(u->text) != 0)
            continue;

        char             response[RESP_MAX];
        response_type_t  resp_type = RESP_MARKDOWN;
        struct timespec  req_start, req_end;

        memset(response, 0, sizeof(response));
        clock_gettime(CLOCK_MONOTONIC, &req_start);

        if (commands_handle(u->text, u->chat_id, u->msg_date,
                            u->user_id, u->username, u->req_id,
                            response, sizeof(response),
                            &resp_type) == 0) {

            LOG_NET(LOG_DEBUG, "req=%04x sending response...", u->req_id);

            if (strncmp(u->text, "/logs",     5) == 0 ||
                strncmp(u->text, "/fail2ban", 9) == 0) {
                telegram_send_plain(u->chat_id, response);
            } else {
                telegram_send_message(u->chat_id, response);
            }

            clock_gettime(CLOCK_MONOTONIC, &req_end);
            long req_ms = elapsed_ms(req_start, req_end);

            /* Обновить метрики времени ответа */
            if (req_ms > g_metrics.max_response_ms) {
                g_metrics.max_response_ms = req_ms;
            }
            /* Скользящее среднее */
            if (g_metrics.cmd_total > 0) {
                g_metrics.avg_response_ms =
                    (g_metrics.avg_response_ms * (g_metrics.cmd_total - 1) + req_ms)
                    / g_metrics.cmd_total;
            } else {
                g_metrics.avg_response_ms = req_ms;
            }

            LOG_NET(LOG_INFO,
                    "poll=%04x req=%04x done: %ld ms (resp=%zu)",
                    g_current_poll_id, u->req_id,
                    req_ms, strlen(response));
        }
    }

    for (int i = 0; i < count; i++) {
        if (updates[i].text)     free((void *)updates[i].text);
        if (updates[i].username) free((void *)updates[i].username);
    }
    free(updates);
}

// ============================================================================
// ГЛАВНАЯ ФУНКЦИЯ ПОЛЛИНГА
// ============================================================================

int telegram_poll(void) {
    static long last_update_id = -1;

    g_poll_cycle++;
    unsigned short poll_id = g_poll_cycle;
    g_current_poll_id = poll_id;

    /* Первый запуск: восстанавливаем offset с диска */
    if (last_update_id == -1) {
        last_update_id = telegram_offset_load();
        telegram_offset_save(last_update_id);
        LOG_NET(LOG_INFO, "Starting poll from offset: %ld", last_update_id);
    }

    char url[URL_MAX];
    snprintf(url, sizeof(url),
             "getUpdates?timeout=25&offset=%ld", last_update_id);
    LOG_NET(LOG_DEBUG, "poll=%04x request (offset=%ld)", poll_id, last_update_id);

    // -----------------------------------------------------------------------
    // Создаём pipe ДО fork
    // -----------------------------------------------------------------------
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        LOG_NET(LOG_ERROR, "poll=%04x pipe() failed: errno=%d", poll_id, errno);
        return -1;
    }

    // -----------------------------------------------------------------------
    // Fork: дочерний процесс изолирует libcurl
    // -----------------------------------------------------------------------
    pid_t pid = fork();

    if (pid == -1) {
        LOG_NET(LOG_ERROR, "poll=%04x fork() failed: errno=%d", poll_id, errno);
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    // ===== ДОЧЕРНИЙ ПРОЦЕСС =====
    if (pid == 0) {
        close(pipefd[0]);   /* read-конец нам не нужен */

        char  *raw_data = NULL;
        size_t raw_size = 0;
        int    rc       = telegram_http_request(url, "", 1, &raw_data, &raw_size);

        FILE *pw = fdopen(pipefd[1], "w");
        if (pw) {
            fprintf(pw, "%d\n", rc);
            if (raw_data && raw_size > 0) {
                fprintf(pw, "%zu\n", raw_size);
                fwrite(raw_data, 1, raw_size, pw);
            } else {
                fprintf(pw, "0\n");
            }
            fflush(pw);
            fclose(pw);   /* EOF на pipe — родитель проснётся */
        } else {
            close(pipefd[1]);
        }

        free(raw_data);
        _exit(0);
    }

    // ===== РОДИТЕЛЬСКИЙ ПРОЦЕСС =====
    close(pipefd[1]);   /* write-конец закрываем немедленно */

    // -----------------------------------------------------------------------
    // Ждём завершения дочернего через poll() на pipe.
    // Возвращает 0 когда pipe readable (дочерний записал данные и завершился).
    // -----------------------------------------------------------------------
    if (wait_for_child(pid, pipefd[0], poll_id) != 0) {
        /* Таймаут или shutdown: дочерний уже убит внутри wait_for_child */
        close(pipefd[0]);
        return -1;
    }

    // -----------------------------------------------------------------------
    // Читаем данные из pipe.
    // Дочерний завершился (pipe EOF) — данные полностью записаны.
    // -----------------------------------------------------------------------
    char chunk_data[RESP_MAX];
    memset(chunk_data, 0, sizeof(chunk_data));
    int http_rc = -1;

    read_pipe_data(pipefd[0], chunk_data, sizeof(chunk_data),
                   &http_rc, poll_id);

    /*
     * pipefd[0] закрыт внутри read_pipe_data (через fclose).
     * Вызываем waitpid — дочерний уже мёртв (pipe EOF это гарантирует),
     * поэтому блокировки здесь нет.
     */
    if (waitpid(pid, NULL, 0) == -1) {
        LOG_NET(LOG_WARN, "poll=%04x waitpid failed: errno=%d", poll_id, errno);
    }

    LOG_NET(LOG_DEBUG, "poll=%04x child PID=%d reaped", poll_id, pid);

    // -----------------------------------------------------------------------
    // Проверяем результат HTTP-запроса
    // -----------------------------------------------------------------------
    if (http_rc != 0) {
        LOG_NET(LOG_WARN, "poll=%04x http request failed (rc=%d)", poll_id, http_rc);
        return -1;
    }

    if (chunk_data[0] == '\0') {
        LOG_NET(LOG_DEBUG, "poll=%04x empty response (timeout with no updates)", poll_id);
        return 0;
    }

    // -----------------------------------------------------------------------
    // Обрабатываем обновления
    // -----------------------------------------------------------------------
    process_updates(chunk_data, strlen(chunk_data), poll_id, &last_update_id);

    return 0;
}
