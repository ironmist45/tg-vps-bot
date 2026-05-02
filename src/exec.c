/**
 * tg-bot - Telegram bot for system administration
 * 
 * exec.c - External command execution with timeout and capture
 * 
 * Provides a robust wrapper for executing external commands via sudo.
 * Features:
 *   - Timeout protection (prevents hung processes)
 *   - Output capture (stdout/stderr)
 *   - Comprehensive status reporting
 *   - Non-blocking I/O with select() and deadline-based timeout
 *   - Graceful termination (SIGTERM → SIGKILL escalation)
 * 
 * All commands are executed through configurable sudo path for privilege escalation.
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

#define _GNU_SOURCE

#include "exec.h"
#include "logger.h"
#include "config.h"
#include "metrics.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

// ============================================================================
// STATUS STRING CONVERSION
// ============================================================================

/**
 * Convert execution status enum to human-readable string
 * 
 * @param s  Execution status
 * @return   String representation (e.g., "OK", "TIMEOUT")
 */
const char* exec_status_str(exec_status_t s) {
    switch (s) {
        case EXEC_OK:            return "OK";
        case EXEC_FORK_FAILED:   return "FORK_FAILED";
        case EXEC_EXEC_FAILED:   return "EXEC_FAILED";
        case EXEC_EXIT_NONZERO:  return "EXIT_NONZERO";
        case EXEC_SIGNAL_KILLED: return "SIGNALED";
        case EXEC_TIMEOUT:       return "TIMEOUT";
        case EXEC_PIPE_FAILED:   return "PIPE_FAILED";
        case EXEC_READ_FAILED:   return "READ_FAILED";
        default:                 return "UNKNOWN";
    }
}

/**
 * Check if command executed successfully
 * 
 * @param r  Execution result
 * @return   1 if status is OK and exit code is 0, 0 otherwise
 */
static int exec_success(const exec_result_t *r) {
    if (!r) return 0;
    return (r->status == EXEC_OK && r->exit_code == 0);
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * Convenience wrapper: execute command and check success
 * 
 * Combines exec_command() and exec_success() into a single call.
 * 
 * @param argv    Command argument array (NULL-terminated)
 * @param output  Buffer for captured output
 * @param size    Size of output buffer
 * @param opts    Execution options (may be NULL for defaults)
 * @param res     Result structure to populate (may be NULL)
 * @return        1 if command succeeded, 0 otherwise
 */
int exec_check_cmd(char *const argv[],
                   char *output,
                   size_t size,
                   const exec_opts_t *opts,
                   exec_result_t *res)
{
    int rc = exec_command(argv, output, size, opts, res);

    if (rc != 0) {
        return 0;
    }

    if (!exec_success(res)) {
        return 0;
    }

    return 1;
}

/**
 * Populate result structure with failure status
 * 
 * @param r       Result structure to populate
 * @param status  Failure status to set
 */
static void exec_result_fail(exec_result_t *r, exec_status_t status)
{
    if (!r) return;

    r->status     = status;
    r->exit_code  = -1;
    r->term_signal = 0;
    r->timed_out  = (status == EXEC_TIMEOUT);
    r->signaled   = 0;
    r->stdout_len = 0;
    r->stderr_len = 0;
    r->duration_ms = 0;
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

/**
 * Kill child process and reap it immediately.
 *
 * Escalation: SIGTERM → 100 ms grace period → SIGKILL → blocking waitpid.
 * Used from all timeout/error paths to guarantee no zombie is left behind.
 *
 * @param pid         Child PID to kill
 * @param fp          Open FILE* wrapping the read-end of the pipe (may be NULL)
 * @param pipefd_r    Raw read-end fd (closed after fclose if fp != NULL)
 * @param log_enabled Whether to emit log messages
 * @param cmdline     Command string for log messages
 */
static void kill_and_reap(pid_t pid, FILE *fp,
                          int log_enabled, const char *cmdline)
{
    kill(pid, SIGTERM);
    usleep(100000);     /* 100 ms grace for SIGTERM */
    kill(pid, SIGKILL);

    /*
     * fclose() closes the underlying fd too, so do it before waitpid()
     * to avoid a short window where the fd is still open while we wait.
     */
    if (fp) fclose(fp);

    waitpid(pid, NULL, 0);  /* blocking: SIGKILL guarantees fast return */

    if (log_enabled) {
        LOG_EXEC(LOG_DEBUG, "killed and reaped cmd='%s' pid=%d", cmdline, pid);
    }
}

/**
 * Compute remaining milliseconds until a CLOCK_MONOTONIC deadline.
 *
 * @param deadline  Absolute deadline (from clock_gettime)
 * @return          Remaining ms, or 0 if deadline already passed
 */
static long deadline_remaining_ms(const struct timespec *deadline)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    long ms = (deadline->tv_sec  - now.tv_sec)  * 1000L
            + (deadline->tv_nsec - now.tv_nsec) / 1000000L;

    return (ms > 0) ? ms : 0;
}

// ============================================================================
// FORWARD DECLARATION
// ============================================================================

static int exec_command_internal(char *const argv[],
                                 char *resp,
                                 size_t size,
                                 const exec_opts_t *opts,
                                 exec_result_t *result);

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Execute external command with full options
 * 
 * This is the primary execution interface. Commands are run via sudo.
 * 
 * @param argv    Command argument array (NULL-terminated)
 * @param output  Buffer for captured output (may be NULL)
 * @param size    Size of output buffer
 * @param opts    Execution options (may be NULL for defaults)
 * @param result  Result structure to populate (may be NULL)
 * @return        0 on success, -1 on failure
 */
int exec_command(char *const argv[],
                 char *output,
                 size_t size,
                 const exec_opts_t *opts,
                 exec_result_t *result)
{
    if (result) {
        memset(result, 0, sizeof(*result));
        result->status = EXEC_OK;
    }

    return exec_command_internal(argv, output, size, opts, result);
}

// ============================================================================
// INTERNAL IMPLEMENTATION
// ============================================================================

/**
 * Core command execution logic
 * 
 * Handles fork/exec, I/O capture, timeout monitoring, and process cleanup.
 *
 * Timeout implementation uses a single absolute deadline computed once at
 * fork time (CLOCK_MONOTONIC). Each select() call receives the remaining
 * time until that deadline, so the total wall-clock timeout is always
 * exactly timeout_ms regardless of how many read iterations occur.
 *
 * After fclose(fp) the child is guaranteed to have exited (EOF on pipe =
 * write-end closed = _exit() called). waitpid() is therefore called once,
 * blocking, and returns immediately — no busy-wait loop, no zombie window.
 */
static int exec_command_internal(char *const argv[],
                                 char *resp,
                                 size_t size,
                                 const exec_opts_t *opts,
                                 exec_result_t *result)
{
    // ------------------------------------------------------------------------
    // Parse options
    // ------------------------------------------------------------------------
    int capture_stderr = 1;
    int log_enabled    = 1;

    if (opts) {
        if (opts->capture_stderr == 0) capture_stderr = 0;
        if (opts->log_output     == 0) log_enabled    = 0;
    }

    // ------------------------------------------------------------------------
    // Validate arguments
    // ------------------------------------------------------------------------
    if (!argv || !argv[0]) {
        if (log_enabled) {
            LOG_EXEC(LOG_ERROR, "invalid argv (empty command)");
        }
        exec_result_fail(result, EXEC_EXEC_FAILED);
        g_metrics.err_exec++;
        return -1;
    }

    // ------------------------------------------------------------------------
    // Initialize output buffer
    // ------------------------------------------------------------------------
    if (resp && size > 0)
        resp[0] = '\0';

    // ------------------------------------------------------------------------
    // Build command line string for logging
    // ------------------------------------------------------------------------
    char cmdline[256] = {0};
    size_t cmd_used = 0;

    for (int i = 0; argv[i]; i++) {
        int written = snprintf(cmdline + cmd_used,
                               sizeof(cmdline) - cmd_used,
                               "%s%s",
                               argv[i],
                               argv[i + 1] ? " " : "");

        if (written < 0 || (size_t)written >= sizeof(cmdline) - cmd_used) {
            if (log_enabled) {
                LOG_EXEC(LOG_WARN, "cmdline truncated: %.200s", cmdline);
            }
            break;
        }

        cmd_used += (size_t)written;
    }

    if (log_enabled) {
        LOG_EXEC(LOG_DEBUG, "start cmd='%s'", cmdline);
    }

    // ------------------------------------------------------------------------
    // Create pipe for child process I/O.
    // O_CLOEXEC ensures the fds are closed automatically on execv() in the
    // child — defensive measure so they are never leaked into grandchildren.
    // ------------------------------------------------------------------------
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0) {
        if (log_enabled) {
            LOG_EXEC(LOG_ERROR, "pipe2 failed cmd='%s' errno=%d", cmdline, errno);
        }
        exec_result_fail(result, EXEC_PIPE_FAILED);
        g_metrics.err_exec++;
        return -1;
    }

    // ------------------------------------------------------------------------
    // Fork child process
    // ------------------------------------------------------------------------
    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    pid_t pid = fork();

    if (pid < 0) {
        if (log_enabled) {
            LOG_EXEC(LOG_ERROR, "fork failed cmd='%s' errno=%d", cmdline, errno);
        }

        close(pipefd[0]);
        close(pipefd[1]);

        clock_gettime(CLOCK_MONOTONIC, &t_end);
        if (result) result->duration_ms = elapsed_ms(t_start, t_end);
        exec_result_fail(result, EXEC_FORK_FAILED);
        return -1;
    }

    // ------------------------------------------------------------------------
    // Child process: redirect I/O and exec
    // ------------------------------------------------------------------------
    if (pid == 0) {
        /*
         * dup2 clears O_CLOEXEC on the new fd numbers (STDOUT/STDERR),
         * so output reaches the pipe even after execv.
         */
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
            (capture_stderr && dup2(pipefd[1], STDERR_FILENO) < 0)) {
            _exit(127);
        }

        /*
         * pipefd[0] and pipefd[1] carry O_CLOEXEC and will be closed
         * automatically by execv. Explicit close here is belt-and-suspenders.
         */
        close(pipefd[0]);
        close(pipefd[1]);

        execv(g_cfg.sudo_path, argv);
        _exit(127);  /* only reached if execv fails */
    }

    // ------------------------------------------------------------------------
    // Parent process: read output with deadline-based timeout
    // ------------------------------------------------------------------------
    close(pipefd[1]);   /* must close write-end so EOF propagates correctly */

    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        close(pipefd[0]);

        clock_gettime(CLOCK_MONOTONIC, &t_end);
        if (result) result->duration_ms = elapsed_ms(t_start, t_end);
        exec_result_fail(result, EXEC_READ_FAILED);
        g_metrics.err_exec++;

        /* Child is still running — must kill and reap to avoid zombie. */
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }

    int timeout_ms = (opts && opts->timeout_ms > 0)
        ? opts->timeout_ms
        : EXEC_DEFAULT_TIMEOUT_MS;

    /*
     * Compute an absolute deadline once.  Every select() call below
     * receives the *remaining* time until this deadline, so the total
     * wall-clock budget is always exactly timeout_ms — even when the
     * child produces output in many small bursts.
     */
    struct timespec deadline = t_start;
    deadline.tv_sec  += timeout_ms / 1000;
    deadline.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    int fd    = pipefd[0];
    size_t used = 0;

    // ------------------------------------------------------------------------
    // Read loop: select() with remaining-deadline timeout per iteration
    // ------------------------------------------------------------------------
    while (1) {
        long remaining = deadline_remaining_ms(&deadline);

        if (remaining == 0) {
            /* Deadline reached before EOF — treat as timeout. */
            if (log_enabled) {
                LOG_EXEC(LOG_ERROR, "timeout cmd='%s' pid=%d", cmdline, pid);
            }
            kill_and_reap(pid, fp, log_enabled, cmdline);

            clock_gettime(CLOCK_MONOTONIC, &t_end);
            if (result) result->duration_ms = elapsed_ms(t_start, t_end);
            exec_result_fail(result, EXEC_TIMEOUT);
            g_metrics.err_exec++;
            return -1;
        }

        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);

        struct timeval tv = {
            .tv_sec  = remaining / 1000,
            .tv_usec = (remaining % 1000) * 1000L
        };

        int rv = select(fd + 1, &set, NULL, NULL, &tv);

        if (rv == 0) {
            /* select() itself reported timeout (remaining just hit zero). */
            if (log_enabled) {
                LOG_EXEC(LOG_ERROR, "timeout cmd='%s' pid=%d", cmdline, pid);
            }
            kill_and_reap(pid, fp, log_enabled, cmdline);

            clock_gettime(CLOCK_MONOTONIC, &t_end);
            if (result) result->duration_ms = elapsed_ms(t_start, t_end);
            exec_result_fail(result, EXEC_TIMEOUT);
            g_metrics.err_exec++;
            return -1;
        }

        if (rv < 0) {
            if (errno == EINTR)
                continue;   /* signal interrupted select — retry with updated deadline */

            if (log_enabled) {
                LOG_EXEC(LOG_ERROR, "select failed cmd='%s' errno=%d", cmdline, errno);
            }
            kill_and_reap(pid, fp, log_enabled, cmdline);

            clock_gettime(CLOCK_MONOTONIC, &t_end);
            if (result) result->duration_ms = elapsed_ms(t_start, t_end);
            exec_result_fail(result, EXEC_READ_FAILED);
            g_metrics.err_exec++;
            return -1;
        }

        /* Buffer full — stop reading, drain the rest on child exit. */
        if (used >= size - 1)
            break;

        /* Data available — read one line. */
        if (fgets(resp + used, size - used, fp) == NULL)
            break;  /* EOF or error — child finished writing */

        size_t len = strlen(resp + used);
        used += len;

        /* Add truncation marker if buffer limit reached. */
        if (used >= size - 1) {
            strncat(resp, "\n...truncated...", size - strlen(resp) - 1);
            break;
        }
    }

    fclose(fp);     /* closes pipefd[0]; EOF is now visible to child if still running */

    // ------------------------------------------------------------------------
    // Wait for child to terminate.
    //
    // At this point fclose() has closed our read-end of the pipe.  The child
    // either already called _exit() (normal case: EOF caused the loop above to
    // break) or it is about to get EPIPE on the next write and exit shortly.
    // Either way, a single *blocking* waitpid() is correct and sufficient —
    // no busy-wait loop, no WNOHANG, no zombie window.
    // ------------------------------------------------------------------------
    int wstatus = 0;
    if (waitpid(pid, &wstatus, 0) == -1) {
        if (log_enabled) {
            LOG_EXEC(LOG_ERROR, "waitpid failed cmd='%s' pid=%d errno=%d",
                     cmdline, pid, errno);
        }
        clock_gettime(CLOCK_MONOTONIC, &t_end);
        if (result) result->duration_ms = elapsed_ms(t_start, t_end);
        exec_result_fail(result, EXEC_READ_FAILED);
        g_metrics.err_exec++;
        return -1;
    }

    // ------------------------------------------------------------------------
    // Populate result structure
    // ------------------------------------------------------------------------
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    long ms = elapsed_ms(t_start, t_end);

    /* Process terminated by signal (e.g. OOM killer, external SIGKILL). */
    if (!WIFEXITED(wstatus)) {
        if (result) {
            result->status     = EXEC_SIGNAL_KILLED;
            result->term_signal = WTERMSIG(wstatus);
            result->duration_ms = ms;
        }
        LOG_EXEC(LOG_WARN, "cmd='%s' killed by signal=%d",
                 cmdline, WTERMSIG(wstatus));
        g_metrics.err_exec++;
        return -1;
    }

    int exit_code = WEXITSTATUS(wstatus);

    /* exit code 127 means execv() itself failed (command not found, etc.) */
    if (exit_code == 127 && result) {
        result->status = EXEC_EXEC_FAILED;
    }

    if (result) {
        result->exit_code   = exit_code;
        result->term_signal = 0;
        result->timed_out   = 0;
        result->signaled    = 0;
        result->stdout_len  = used;
        result->stderr_len  = 0;
        result->duration_ms = ms;

        if (exit_code == 0)
            result->status = EXEC_OK;
        else if (result->status != EXEC_EXEC_FAILED)
            result->status = EXEC_EXIT_NONZERO;
    }

    // ------------------------------------------------------------------------
    // Log execution summary
    // ------------------------------------------------------------------------
    if (log_enabled) {
        int quiet = (opts && opts->quiet);

        if (quiet) {
            LOG_EXEC_CHECK(LOG_DEBUG,
                "cmd='%s' status=%s exit=%d time=%ldms out=%zuB",
                cmdline,
                result ? exec_status_str(result->status) : "N/A",
                exit_code, ms, used);
        } else {
            LOG_EXEC(LOG_INFO,
                "cmd='%s' status=%s exit=%d time=%ldms out=%zuB",
                cmdline,
                result ? exec_status_str(result->status) : "N/A",
                exit_code, ms, used);
        }
    }

    /* Log output snippet on failure for easier debugging. */
    if (exit_code != 0 && log_enabled) {
        int quiet = (opts && opts->quiet);

        if (quiet) {
            LOG_EXEC_CHECK(LOG_DEBUG,
                "cmd='%s' failed status=%s exit=%d",
                cmdline,
                result ? exec_status_str(result->status) : "N/A",
                exit_code);
            if (resp && resp[0])
                LOG_EXEC_CHECK(LOG_DEBUG, "output: %.200s", resp);
        } else {
            LOG_EXEC(LOG_WARN,
                "cmd='%s' failed status=%s exit=%d",
                cmdline,
                result ? exec_status_str(result->status) : "N/A",
                exit_code);
            if (resp && resp[0])
                LOG_EXEC(LOG_WARN, "output: %.200s", resp);
        }
    }

    return 0;
}
