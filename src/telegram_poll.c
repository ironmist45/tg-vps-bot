/* tg-bot — telegram_poll.c — Long polling with fork isolation. MIT License © 2026 ironmist45 */

/*
 * libcurl isolation architecture:
 *   HTTP request to Telegram API runs in a child process.
 *   This protects the parent from libcurl hangs and allows graceful
 *   shutdown on SIGTERM without waiting for curl.
 *
 *   Interaction diagram:
 *     parent                        child
 *       |-- fork() ----------------> |
 *       |                            |-- telegram_http_request()
 *       |-- poll(pipe, POLLIN) <-----|-- write(pipe, result)
 *       |                            |-- _exit(0)
 *       |-- read(pipe) --------------|
 *       |-- waitpid(blocking) -------|  (child already dead)
 *
 *   Order: read pipe data first, then waitpid().
 *   This guarantees zero zombie window from the observer's perspective:
 *   by the time waitpid() is called the child has already exited
 *   (pipe EOF = child closed its write end = _exit() was called).
 */

#define _GNU_SOURCE

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
#include "utils.h"
#include "telegram_timeouts.h"
#include "cmd_upload.h"
#include "config.h"    /* g_cfg.upload_enabled */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>

/*
 * Maximum URL length for Telegram API request.
 * Accounts for getUpdates path + timeout + offset parameters.
 */
#define URL_MAX 1024

/*
 * Bot response buffer size — limits the text sent back to the user
 * per command (e.g. /services, /logs output). This is intentionally
 * capped: Telegram messages have a 4096-char limit and responses are
 * formatted to fit. Not related to the incoming JSON buffer from Telegram.
 */
#define BOT_RESP_MAX 8192

/*
 * Interval between shutdown checks inside wait_for_child().
 * poll() wakes up every N ms to check g_shutdown_requested.
 */
#define POLL_SHUTDOWN_CHECK_MS 200

/*
 * Buffer size for packing file_id and file_name into ctx.args.
 * Format: "<file_id>\n<file_name>" — both fields separated by '\n'.
 * UPLOAD_FILE_ID_MAX (256) + '\n' + UPLOAD_FILENAME_MAX (128) + '\0' = 386.
 * 512 provides comfortable headroom.
 */
#define UPLOAD_ARGS_BUF_MAX 512

static long           g_last_processed_update = 0;
static int            g_offset_counter        = 0;
static unsigned short g_poll_cycle            = 0;
static unsigned short g_current_poll_id       = 0;

/*
 * Combined RSS of parent + child, sampled right after fork() while both
 * processes are alive. Updated every poll cycle. Read by selfcheck() in
 * cmd_system.c to report memory usage in /start without scanning /proc.
 */
long g_poll_rss_kb    = 0;
int  g_poll_rss_procs = 0;

// ============================================================================
// INITIALIZATION
// ============================================================================

void telegram_poll_init(void) {
    LOG_NET(LOG_INFO, "Telegram poll module initialized");
}

unsigned short telegram_get_poll_id(void) {
    return g_current_poll_id;
}

// ============================================================================
// RSS SAMPLING
// ============================================================================

/*
 * Read VmRSS from a /proc/<pid>/status file.
 * Returns RSS in KB, or 0 on error.
 */
static long read_proc_rss_kb(pid_t pid)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    FILE *fp = fopen(path, "r");
    if (!fp) return 0;

    long rss = 0;
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%ld", &rss);
            break;
        }
    }
    fclose(fp);
    return rss;
}

/*
 * Sample combined RSS of parent + child immediately after fork().
 * Both processes are guaranteed alive at this point.
 * Updates g_poll_rss_kb and g_poll_rss_procs.
 */
static void sample_rss(pid_t child_pid)
{
    long parent_rss = read_proc_rss_kb(getpid());
    long child_rss  = read_proc_rss_kb(child_pid);

    long total = 0;
    int  procs = 0;

    if (parent_rss > 0) { total += parent_rss; procs++; }
    if (child_rss  > 0) { total += child_rss;  procs++; }

    if (total > 0) {
        g_poll_rss_kb    = total;
        g_poll_rss_procs = procs;
    }
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/*
 * kill_and_reap - safely kill child process and collect its exit status.
 *
 * Used in timeout and shutdown paths.
 * After SIGKILL the child cannot survive, so waitpid() is blocking —
 * it will return very quickly.
 */
static void kill_and_reap(pid_t pid, unsigned short poll_id) {
    if (kill(pid, SIGKILL) == 0)
        LOG_NET(LOG_DEBUG, "poll=%04x SIGKILL sent to child PID=%d", poll_id, pid);

    if (waitpid(pid, NULL, 0) == -1)
        LOG_NET(LOG_WARN, "poll=%04x waitpid after kill failed: errno=%d", poll_id, errno);
}

/*
 * read_pipe_data - read data from pipe and return a heap-allocated buffer.
 *
 * Protocol (written by child):
 *   "<rc>\n<data_len>\n<data_bytes>"
 *
 * Allocates exactly data_len+1 bytes via malloc so the buffer fits any
 * response size without a fixed cap. Caller must free(*out_data).
 * *out_data is set to NULL on error or when data_len == 0.
 * *out_size is set to the number of bytes read (excluding the null terminator).
 *
 * Returns http_rc via out_http_rc.
 */
static void read_pipe_data(int fd,
                           char **out_data,
                           size_t *out_size,
                           int *out_http_rc,
                           unsigned short poll_id)
{
    *out_http_rc = -1;
    *out_data    = NULL;
    *out_size    = 0;

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

    /* Allocate exact size — no fixed cap, handles any response size */
    char *buf = malloc(data_len + 1);
    if (!buf) {
        LOG_NET(LOG_ERROR, "poll=%04x malloc(%zu) failed (OOM)", poll_id, data_len + 1);
        fclose(pipe_read);
        return;
    }

    size_t bytes_read = fread(buf, 1, data_len, pipe_read);
    buf[bytes_read] = '\0';

    LOG_NET(LOG_DEBUG,
            "poll=%04x pipe: rc=%d data_len=%zu read=%zu first100=%.100s",
            poll_id, rc, data_len, bytes_read, buf);

    fclose(pipe_read);

    *out_data = buf;
    *out_size = bytes_read;
}

// ============================================================================
// CHILD PROCESS WAIT (KEY BLOCK)
// ============================================================================

/*
 * wait_for_child - wait for child process completion via poll() on pipe.
 *
 * Why poll() on pipe instead of WNOHANG loop:
 *
 *   Old approach: waitpid(WNOHANG) + poll(NULL, 0, 100ms)
 *     - busy-wait with 100ms pause
 *     - zombie window up to 100ms (visible in ps)
 *     - unnecessary loop iterations
 *
 *   New approach: poll(pipefd[0], POLLIN, timeout)
 *     - pipe becomes readable exactly when the child closes its write end
 *       (fclose(pipe_write) -> _exit(0))
 *     - we wake up instantly, no polling
 *     - after poll() returns the child has already exited or is exiting
 *       now -> waitpid(blocking) returns immediately, no zombie window
 *     - shutdown is checked on every EINTR — instant reaction
 *
 * Returns:
 *   0  — child exited normally, pipe data is ready
 *  -1  — timeout or shutdown (child was killed, pipe closed)
 */
static int wait_for_child(pid_t pid, int pipe_read_fd,
                          unsigned short poll_id)
{
    struct pollfd pfd = {
        .fd     = pipe_read_fd,
        .events = POLLIN
    };

    int remaining_ms = TG_CHILD_WAIT_TIMEOUT_MS;

    while (remaining_ms > 0) {

        /* Check shutdown before calling poll() */
        if (g_signal_received != 0 || g_shutdown_requested != 0) {
            LOG_NET(LOG_WARN,
                    "poll=%04x shutdown requested, killing child PID=%d "
                    "(shutdown=%d signal=%d)",
                    poll_id, pid,
                    g_shutdown_requested, g_signal_received);
            kill_and_reap(pid, poll_id);
            return -1;
        }

        /* Wake up every POLL_SHUTDOWN_CHECK_MS ms to check shutdown */
        int rc = poll(&pfd, 1, POLL_SHUTDOWN_CHECK_MS);

        if (rc > 0) {
            /*
             * Pipe readable (POLLIN) or child closed its end (POLLHUP).
             * Both mean: child has exited or is exiting. Data is ready.
             */
            if (pfd.revents & (POLLIN | POLLHUP)) {
                LOG_NET(LOG_DEBUG,
                        "poll=%04x pipe ready (revents=0x%x), child PID=%d done",
                        poll_id, pfd.revents, pid);
                return 0;
            }
            /* POLLERR / POLLNVAL — unexpected condition */
            LOG_NET(LOG_WARN,
                    "poll=%04x pipe error (revents=0x%x)",
                    poll_id, pfd.revents);
            kill_and_reap(pid, poll_id);
            return -1;
        }

        if (rc == 0) {
            /* POLL_SHUTDOWN_CHECK_MS slice expired, reduce remaining time */
            remaining_ms -= POLL_SHUTDOWN_CHECK_MS;
            continue;
        }

        /* rc < 0 */
        if (errno == EINTR) {
            /* Interrupted by signal — check shutdown at next iteration */
            LOG_NET(LOG_DEBUG, "poll=%04x poll() interrupted by signal", poll_id);
            continue;
        }

        /* Unexpected poll() error */
        LOG_NET(LOG_ERROR, "poll=%04x poll() error: errno=%d", poll_id, errno);
        kill_and_reap(pid, poll_id);
        return -1;
    }

    /* Overall timeout exhausted */
    LOG_NET(LOG_ERROR,
            "poll=%04x child PID=%d timed out after %d ms, killing",
            poll_id, pid, TG_CHILD_WAIT_TIMEOUT_MS);
    kill_and_reap(pid, poll_id);
    return -1;
}

// ============================================================================
// UPDATE PROCESSING
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

        if (!security_is_allowed_chat(u->chat_id)) {
            g_metrics.err_unauthorized++;
            LOG_NET(LOG_WARN,
                    "poll=%04x req=%04x ACCESS CHECK: "
                    "chat_id=%ld result=DENIED",
                    g_current_poll_id, u->req_id, u->chat_id);
            continue;
        }

        char             response[BOT_RESP_MAX];
        response_type_t  resp_type = RESP_MARKDOWN;
        struct timespec  req_start, req_end;

        memset(response, 0, sizeof(response));
        clock_gettime(CLOCK_MONOTONIC, &req_start);

        /* ---------------------------------------------------------------
         * Dispatch: file upload or text command
         * --------------------------------------------------------------- */
        if (u->file_id) {
            /*
             * Incoming document — route to upload handler.
             *
             * Pack file_id and file_name into args_buf as
             * "<file_id>\n<file_name>" so cmd_handle_upload() can
             * extract both fields from ctx->args without extra fields
             * in command_ctx_t.
             */
            LOG_NET(LOG_INFO,
                    "poll=%04x req=%04x upd=%ld incoming: "
                    "chat_id=%ld file_id=%.8s name=%s size=%d",
                    g_current_poll_id, u->req_id, u->update_id,
                    u->chat_id,
                    u->file_id,
                    u->file_name ? u->file_name : "(none)",
                    u->file_size);
            LOG_SEC(LOG_INFO,
                    "poll=%04x req=%04x ACCESS CHECK: chat_id=%ld result=ALLOW (file)",
                    g_current_poll_id, u->req_id, u->chat_id);

            if (!g_cfg.upload_enabled) {
                LOG_NET(LOG_INFO,
                        "poll=%04x req=%04x file upload ignored (UPLOAD_ENABLED=no)",
                        g_current_poll_id, u->req_id);
                telegram_send_plain(u->chat_id, "⚠️ File upload is disabled");
                continue;
            }

            char args_buf[UPLOAD_ARGS_BUF_MAX];
            snprintf(args_buf, sizeof(args_buf), "%s\n%s\n%d",
                     u->file_id,
                     u->file_name ? u->file_name : "",
                     u->file_size);

            command_ctx_t ctx = {
                .chat_id   = u->chat_id,
                .user_id   = u->user_id,
                .username  = u->username,
                .args      = args_buf,
                .raw_text  = "",
                .msg_date  = u->msg_date,
                .response  = response,
                .resp_size = sizeof(response),
                .resp_type = &resp_type,
                .req_id    = u->req_id
            };

            int rc = cmd_handle_upload(&ctx);

            if (response[0] != '\0') {
                if (resp_type == RESP_PLAIN)
                    telegram_send_plain(u->chat_id, response);
                else
                    telegram_send_message(u->chat_id, response);
            }

            clock_gettime(CLOCK_MONOTONIC, &req_end);
            long req_ms = elapsed_ms(req_start, req_end);

            LOG_NET(LOG_INFO,
                    "poll=%04x req=%04x upload done: rc=%d %ld ms",
                    g_current_poll_id, u->req_id, rc, req_ms);

        } else if (u->text) {
            /*
             * Text command — existing dispatch path.
             */
            LOG_NET(LOG_INFO,
                    "poll=%04x req=%04x upd=%ld incoming: "
                    "chat_id=%ld len=%zu text=%.64s",
                    g_current_poll_id, u->req_id, u->update_id,
                    u->chat_id, strlen(u->text), u->text);

            if (security_validate_text(u->text) != 0)
                continue;

            if (commands_handle(u->text, u->chat_id, u->msg_date,
                                u->user_id, u->username, u->req_id,
                                response, sizeof(response),
                                &resp_type) == 0) {

                LOG_NET(LOG_DEBUG, "req=%04x sending response (type=%s)...",
                        u->req_id,
                        (resp_type == RESP_PLAIN) ? "plain" : "markdown");

                if (resp_type == RESP_PLAIN)
                    telegram_send_plain(u->chat_id, response);
                else
                    telegram_send_message(u->chat_id, response);

                clock_gettime(CLOCK_MONOTONIC, &req_end);
                long req_ms = elapsed_ms(req_start, req_end);

                /* Update response time metrics */
                if (req_ms > g_metrics.max_response_ms)
                    g_metrics.max_response_ms = req_ms;

                /* Rolling average */
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
        /* else: update has neither text nor file_id — already filtered by parser */
    }

    /* Free all heap-allocated fields in each update */
    for (int i = 0; i < count; i++) {
        free((void *)updates[i].text);
        free((void *)updates[i].username);
        free((void *)updates[i].file_id);
        free((void *)updates[i].file_name);
    }
    free(updates);
}

// ============================================================================
// MAIN POLL FUNCTION
// ============================================================================

int telegram_poll(void) {
    static long last_update_id = -1;

    g_poll_cycle++;
    unsigned short poll_id = g_poll_cycle;
    g_current_poll_id = poll_id;

    /* First run: restore offset from disk */
    if (last_update_id == -1) {
        last_update_id = telegram_offset_load();
        telegram_offset_save(last_update_id);
        LOG_NET(LOG_INFO, "Starting poll from offset: %ld", last_update_id);
    }

    char url[URL_MAX];
    snprintf(url, sizeof(url),
             "getUpdates?timeout=%d&offset=%ld",
             TG_POLL_TIMEOUT_SEC, last_update_id);
    LOG_NET(LOG_DEBUG, "poll=%04x request (offset=%ld)", poll_id, last_update_id);

    // -----------------------------------------------------------------------
    // Create pipe BEFORE fork.
    // O_CLOEXEC: fds are closed automatically on execv() in the child —
    // prevents fd leaks into child processes.
    // -----------------------------------------------------------------------
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) == -1) {
        LOG_NET(LOG_ERROR, "poll=%04x pipe2() failed: errno=%d", poll_id, errno);
        return -1;
    }

    // -----------------------------------------------------------------------
    // Fork: child process isolates libcurl
    // -----------------------------------------------------------------------
    pid_t pid = fork();

    if (pid == -1) {
        LOG_NET(LOG_ERROR, "poll=%04x fork() failed: errno=%d", poll_id, errno);
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    // ===== CHILD PROCESS =====
    // Note: if malloc fails inside telegram_http_request or fdopen,
    // the child will crash (SIGSEGV). Parent detects this via WIFSIGNALED
    // and logs "child killed by signal=11". This is intentional —
    // no recovery logic belongs in the child.
    if (pid == 0) {
        close(pipefd[0]);   /* read end not needed in child */

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
            fclose(pw);   /* EOF on pipe — parent will wake up */
        } else {
            close(pipefd[1]);
        }

        free(raw_data);
        _exit(0);
    }

    // ===== PARENT PROCESS =====
    close(pipefd[1]);   /* close write end immediately */

    // -----------------------------------------------------------------------
    // Sample RSS while both processes are guaranteed alive.
    // Child was just created via fork() and has not yet exited —
    // this is the only reliable moment to read its RSS.
    // Result is stored in g_poll_rss_kb / g_poll_rss_procs and
    // read by selfcheck() when handling the /start command.
    // -----------------------------------------------------------------------
    sample_rss(pid);

    // -----------------------------------------------------------------------
    // Wait for child via poll() on pipe.
    // Returns 0 when pipe is readable (child wrote data and exited).
    // -----------------------------------------------------------------------
    if (wait_for_child(pid, pipefd[0], poll_id) != 0) {
        /* Timeout or shutdown: child was already killed inside wait_for_child */
        close(pipefd[0]);
        return -1;
    }

    // -----------------------------------------------------------------------
    // Read data from pipe into a heap-allocated buffer.
    // read_pipe_data() allocates exactly data_len+1 bytes so there is no
    // fixed cap — any size JSON response from Telegram is handled correctly.
    // Caller (this function) is responsible for free(chunk_data).
    // -----------------------------------------------------------------------
    char  *chunk_data = NULL;
    size_t chunk_size = 0;
    int    http_rc    = -1;

    read_pipe_data(pipefd[0], &chunk_data, &chunk_size, &http_rc, poll_id);

    /*
     * pipefd[0] is closed inside read_pipe_data (via fclose).
     * Call waitpid — child is already dead (pipe EOF guarantees this),
     * so there is no blocking here. Log a warning if child was killed
     * by a signal (unexpected crash).
     */
    int wstatus = 0;
    if (waitpid(pid, &wstatus, 0) == -1) {
        LOG_NET(LOG_WARN, "poll=%04x waitpid failed: errno=%d", poll_id, errno);
    } else if (WIFSIGNALED(wstatus)) {
        LOG_NET(LOG_WARN, "poll=%04x child killed by signal=%d",
                poll_id, WTERMSIG(wstatus));
    }

    LOG_NET(LOG_DEBUG, "poll=%04x child PID=%d reaped", poll_id, pid);

    // -----------------------------------------------------------------------
    // Check HTTP request result
    // -----------------------------------------------------------------------
    if (http_rc != 0) {
        LOG_NET(LOG_WARN, "poll=%04x http request failed (rc=%d)", poll_id, http_rc);
        free(chunk_data);
        return -1;
    }

    if (!chunk_data) {
        if (http_rc == -1) {
            /* Pipe read error — already logged inside read_pipe_data */
            return -1;
        }
        /* http_rc == 0, data_len == 0 — normal Telegram long-poll timeout */
        LOG_NET(LOG_DEBUG, "poll=%04x timeout (no updates)", poll_id);
        return 0;
    }

    // -----------------------------------------------------------------------
    // Process updates
    // -----------------------------------------------------------------------
    process_updates(chunk_data, chunk_size, poll_id, &last_update_id);

    free(chunk_data);
    return 0;
}
