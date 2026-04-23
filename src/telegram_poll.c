/**
 * tg-bot - Telegram bot for system administration
 * telegram_poll.c - Long polling implementation with fork isolation
 * MIT License - Copyright (c) 2026
 */

#include "telegram.h"
#include "telegram_poll.h"
#include "telegram_http.h"
#include "telegram_parser.h"
#include "telegram_offset.h"
#include "logger.h"
#include "commands.h"
#include "security.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <curl/curl.h>

// External flags from lifecycle.c
extern volatile sig_atomic_t g_shutdown_requested;
extern volatile sig_atomic_t g_signal_received;

#define URL_MAX   1024
#define RESP_MAX  8192

static long g_last_processed_update = 0;
static int g_offset_counter = 0;
static unsigned short g_poll_cycle = 0;
static unsigned short g_current_poll_id = 0;

unsigned short telegram_get_poll_id(void) {
    return g_current_poll_id;
}

int telegram_poll() {
    static long last_update_id = -1;

    g_poll_cycle++;
    unsigned short poll_id = g_poll_cycle;
    g_current_poll_id = poll_id;

    if (last_update_id == -1) {
        last_update_id = telegram_offset_load();
        LOG_NET(LOG_INFO, "Starting poll from offset: %ld", last_update_id);
    }

    char url[URL_MAX];
    snprintf(url, sizeof(url), "getUpdates?timeout=25&offset=%ld", last_update_id);
    LOG_NET(LOG_DEBUG, "poll=%04x request (offset=%ld)", poll_id, last_update_id);

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        LOG_NET(LOG_ERROR, "poll=%04x pipe failed", poll_id);
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        LOG_NET(LOG_ERROR, "poll=%04x fork failed", poll_id);
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        
        char *raw_data = NULL;
        size_t raw_size = 0;
        int rc = telegram_http_request(url, "", 1, &raw_data, &raw_size);
        
        FILE *pipe_write = fdopen(pipefd[1], "w");
        if (pipe_write) {
            fprintf(pipe_write, "%d\n", rc);
            if (raw_data) {
                fprintf(pipe_write, "%zu\n", raw_size);
                fwrite(raw_data, 1, raw_size, pipe_write);
            } else {
                fprintf(pipe_write, "0\n");
            }
            fflush(pipe_write);
            fclose(pipe_write);
        }
        
        free(raw_data);
        _exit(0);
    }

    close(pipefd[1]);

    char chunk_data[RESP_MAX];
    memset(chunk_data, 0, sizeof(chunk_data));
    int http_rc = -1;
    int data_received = 0;
    
    time_t start_wait = time(NULL);
    int wait_timeout = 35;

    LOG_NET(LOG_DEBUG, "poll=%04x parent waiting for child (PID=%d)", poll_id, pid);

    while (1) {
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        
        if (result == pid) {
            LOG_NET(LOG_DEBUG, "poll=%04x child process exited", poll_id);
            if (WIFEXITED(status) && !data_received) {
                FILE *pipe_read = fdopen(pipefd[0], "r");
                if (pipe_read) {
                    size_t data_len;
                    if (fscanf(pipe_read, "%d\n%zu\n", &http_rc, &data_len) == 2) {
                        if (data_len > 0 && data_len < RESP_MAX) {
                            fread(chunk_data, 1, data_len, pipe_read);
                        }
                    }
                    fclose(pipe_read);
                    data_received = 1;
                }
            }
            break;
        }

        if (g_signal_received != 0 || g_shutdown_requested != 0) {
            LOG_NET(LOG_WARN, "poll=%04x aborting child process (parent_pid=%d, child_pid=%d, shutdown=%d, signal=%d)", 
                    poll_id, getpid(), pid, g_shutdown_requested, g_signal_received);
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            close(pipefd[0]);
            return -1;
        }

        if (time(NULL) - start_wait > wait_timeout) {
            LOG_NET(LOG_ERROR, "poll=%04x child process timed out after %d seconds. Killing.", poll_id, wait_timeout);
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            close(pipefd[0]);
            return -1;
        }

        int poll_rc = poll(NULL, 0, 100);
        if (poll_rc == -1 && errno == EINTR) {
            LOG_NET(LOG_DEBUG, "poll=%04x interrupted by signal", poll_id);
        }
        
        LOG_NET(LOG_DEBUG, "poll=%04x loop: shutdown=%d, signal=%d, child_alive=%d", 
                poll_id, g_shutdown_requested, g_signal_received, (kill(pid, 0) == 0));
    }

    close(pipefd[0]);

    if (http_rc != 0 || strlen(chunk_data) == 0) {
        return (http_rc == 0) ? 0 : -1;
    }

    telegram_update_t *updates = NULL;
    int count = 0;
    if (telegram_parse_updates(chunk_data, strlen(chunk_data), poll_id, &updates, &count) != 0) {
        return -1;
    }

    for (int i = 0; i < count; i++) {
        telegram_update_t *u = &updates[i];
        if (u->update_id <= g_last_processed_update) continue;
        
        g_last_processed_update = u->update_id;
        last_update_id = u->update_id + 1;
        telegram_offset_save(last_update_id);

        if (++g_offset_counter >= 5) {
            LOG_NET(LOG_DEBUG, "saving offset: %ld", last_update_id);
            telegram_offset_save(last_update_id);
            g_offset_counter = 0;
        }

        LOG_NET(LOG_INFO, "poll=%04x req=%04x upd=%ld incoming: chat_id=%ld len=%zu text=%.64s",
                g_current_poll_id, u->req_id, u->update_id, u->chat_id, strlen(u->text), u->text);

        if (!security_is_allowed_chat(u->chat_id)) {
            LOG_NET(LOG_WARN, "poll=%04x req=%04x ACCESS CHECK: chat_id=%ld cmd=%s result=DENIED",
                    g_current_poll_id, u->req_id, u->chat_id, u->text);
            continue;
        }
        if (security_validate_text(u->text) != 0) continue;

        char response[RESP_MAX];
        response_type_t resp_type;
        struct timespec req_start, req_end;
        clock_gettime(CLOCK_MONOTONIC, &req_start);

        if (commands_handle(u->text, u->chat_id, u->msg_date, u->user_id, u->username, u->req_id,
                            response, sizeof(response), &resp_type) == 0) {
            
            clock_gettime(CLOCK_MONOTONIC, &req_end);
            long req_ms = (req_end.tv_sec - req_start.tv_sec) * 1000 + (req_end.tv_nsec - req_start.tv_nsec) / 1000000;
            LOG_NET(LOG_INFO, "poll=%04x req=%04x done: %ld ms (resp=%zu)",
                    g_current_poll_id, u->req_id, req_ms, strlen(response));
        }
    }
    
    free(updates);
    return 0;
}
