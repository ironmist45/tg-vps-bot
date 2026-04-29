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
 *   - Non-blocking I/O with select()
 *   - Graceful termination (SIGTERM → SIGKILL escalation)
 * 
 * All commands are executed through /usr/bin/sudo for privilege escalation.
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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
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
int exec_success(const exec_result_t *r) {
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

    r->status = status;
    r->exit_code = -1;
    r->term_signal = 0;
    r->timed_out = (status == EXEC_TIMEOUT);
    r->signaled = 0;
    r->stdout_len = 0;
    r->stderr_len = 0;
    r->duration_ms = 0;
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
 * Use this instead of exec_command_simple() for better log diagnostics.
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

/**
 * Execute external command with default options
 * 
 * Simplified interface for cases where options and result are not needed.
 * Kept for backward compatibility. Prefer exec_command() with explicit
 * result parameter for better log diagnostics (avoids status=N/A).
 * 
 * @param argv    Command argument array (NULL-terminated)
 * @param output  Buffer for captured output
 * @param size    Size of output buffer
 * @return        0 on success, -1 on failure
 */
int exec_command_simple(char *const argv[],
                        char *output,
                        size_t size)
{
    return exec_command(argv, output, size, NULL, NULL);
}

// ============================================================================
// INTERNAL IMPLEMENTATION
// ============================================================================

/**
 * Core command execution logic
 * 
 * Handles fork/exec, I/O capture, timeout monitoring, and process cleanup.
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
    int log_enabled = 1;

    if (opts) {
        if (opts->capture_stderr == 0)
            capture_stderr = 0;
        if (opts->log_output == 0)
            log_enabled = 0;
    }

    // ------------------------------------------------------------------------
    // Validate arguments
    // ------------------------------------------------------------------------
    if (!argv || !argv[0]) {
        if (log_enabled) {
            LOG_EXEC(LOG_ERROR, "invalid argv (empty command)");
        }
        exec_result_fail(result, EXEC_EXEC_FAILED);
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
    // Create pipe for child process I/O
    // ------------------------------------------------------------------------
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        if (log_enabled) {
            LOG_EXEC(LOG_ERROR, "pipe failed cmd='%s'", cmdline);
        }
        exec_result_fail(result, EXEC_PIPE_FAILED);
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
            LOG_EXEC(LOG_ERROR, "fork failed cmd='%s'", cmdline);
        }

        close(pipefd[0]);
        close(pipefd[1]);

        clock_gettime(CLOCK_MONOTONIC, &t_end);
        long elapsed_ms = (t_end.tv_sec - t_start.tv_sec) * 1000 +
                          (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

        if (result) result->duration_ms = elapsed_ms;
        exec_result_fail(result, EXEC_FORK_FAILED);
        return -1;
    }

    // ------------------------------------------------------------------------
    // Child process: setup I/O and exec
    // ------------------------------------------------------------------------
    if (pid == 0) {
        // Redirect stdout and optionally stderr to pipe
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
            (capture_stderr && dup2(pipefd[1], STDERR_FILENO) < 0)) {
            _exit(127);
        }

        close(pipefd[0]);
        close(pipefd[1]);

        // Execute command via sudo
        execv("/usr/bin/sudo", argv);
        _exit(127);  // Only reached if execv fails
    }

    // ------------------------------------------------------------------------
    // Parent process: read output with timeout
    // ------------------------------------------------------------------------
    close(pipefd[1]);

    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        close(pipefd[0]);

        clock_gettime(CLOCK_MONOTONIC, &t_end);
        long elapsed_ms = (t_end.tv_sec - t_start.tv_sec) * 1000 +
                          (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

        if (result) result->duration_ms = elapsed_ms;
        exec_result_fail(result, EXEC_READ_FAILED);
        return -1;
    }

    size_t used = 0;
    int fd = pipefd[0];

    int timeout_ms = (opts && opts->timeout_ms > 0)
        ? opts->timeout_ms
        : EXEC_DEFAULT_TIMEOUT_MS;

    // ------------------------------------------------------------------------
    // Read loop with select() for timeout support
    // ------------------------------------------------------------------------
    while (1) {
        fd_set set;
        struct timeval timeout;

        FD_ZERO(&set);
        FD_SET(fd, &set);

        timeout.tv_sec  = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;

        int rv = select(fd + 1, &set, NULL, NULL, &timeout);

        // Timeout occurred
        if (rv == 0) {
            if (log_enabled) {
                LOG_EXEC(LOG_ERROR, "timeout cmd='%s' pid=%d", cmdline, pid);
            }

            // Escalate: SIGTERM → wait 100ms → SIGKILL
            kill(pid, SIGTERM);
            usleep(100000);
            kill(pid, SIGKILL);

            fclose(fp);
            waitpid(pid, NULL, 0);

            clock_gettime(CLOCK_MONOTONIC, &t_end);
            long elapsed_ms = (t_end.tv_sec - t_start.tv_sec) * 1000 +
                              (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

            if (result) result->duration_ms = elapsed_ms;
            exec_result_fail(result, EXEC_TIMEOUT);
            return -1;
        }

        // Select error (retry on EINTR)
        if (rv < 0) {
            if (errno == EINTR)
                continue;

            if (log_enabled) {
                LOG_EXEC(LOG_ERROR, "select failed cmd='%s'", cmdline);
            }

            fclose(fp);
            waitpid(pid, NULL, 0);

            clock_gettime(CLOCK_MONOTONIC, &t_end);
            long elapsed_ms = (t_end.tv_sec - t_start.tv_sec) * 1000 +
                              (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

            if (result) result->duration_ms = elapsed_ms;
            exec_result_fail(result, EXEC_READ_FAILED);
            return -1;
        }

        // Buffer full
        if (used >= size - 1)
            break;

        // Read available data
        if (fgets(resp + used, size - used, fp) == NULL)
            break;

        size_t len = strlen(resp + used);
        used += len;

        // Add truncation marker if buffer limit reached
        if (used >= size - 1) {
            strncat(resp, "\n...truncated...", size - strlen(resp) - 1);
            break;
        }
    }

    fclose(fp);

    // ------------------------------------------------------------------------
    // Wait for child process to terminate
    // ------------------------------------------------------------------------
    int status = 0;
    int waited = 0;
    int finished = 0;

    while (waited < timeout_ms) {
        pid_t rc = waitpid(pid, &status, WNOHANG);

        if (rc == pid) {
            finished = 1;
            break;
        }

        if (rc == -1) {
            if (log_enabled) {
                LOG_EXEC(LOG_ERROR, "waitpid failed cmd='%s' pid=%d", cmdline, pid);
            }

            clock_gettime(CLOCK_MONOTONIC, &t_end);
            long elapsed_ms = (t_end.tv_sec - t_start.tv_sec) * 1000 +
                              (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

            if (result) result->duration_ms = elapsed_ms;
            exec_result_fail(result, EXEC_READ_FAILED);
            return -1;
        }

        usleep(100000);
        waited += 100;
    }

    // Process didn't terminate within timeout
    if (!finished) {
        if (log_enabled) {
            LOG_EXEC(LOG_WARN, "exec timeout (%d ms): %s (pid=%d)",
                     timeout_ms, cmdline, pid);
        }

        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);

        clock_gettime(CLOCK_MONOTONIC, &t_end);
        long elapsed_ms = (t_end.tv_sec - t_start.tv_sec) * 1000 +
                          (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

        if (result) result->duration_ms = elapsed_ms;
        exec_result_fail(result, EXEC_TIMEOUT);
        return -1;
    }

    // ------------------------------------------------------------------------
    // Populate result structure
    // ------------------------------------------------------------------------
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    long elapsed_ms = (t_end.tv_sec - t_start.tv_sec) * 1000 +
                      (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

    // Process terminated by signal
    if (!WIFEXITED(status)) {
        if (result) {
            result->status = EXEC_SIGNAL_KILLED;
            result->term_signal = WTERMSIG(status);
        }

        LOG_EXEC(LOG_WARN, "cmd='%s' killed by signal=%d", cmdline, WTERMSIG(status));
        return -1;
    }
    
    int exit_code = WEXITSTATUS(status);

    // Exit code 127 indicates execv failed
    if (exit_code == 127 && result) {
        result->status = EXEC_EXEC_FAILED;
    }
    
    if (result) {
        result->exit_code = exit_code;
        result->term_signal = 0;
        result->timed_out = 0;
        result->signaled = 0;
        result->stdout_len = used;
        result->stderr_len = 0;
        result->duration_ms = elapsed_ms;

        if (exit_code == 0)
            result->status = EXEC_OK;
        else
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
                exit_code,
                elapsed_ms,
                used);
        } else {
            LOG_EXEC(LOG_INFO,
                "cmd='%s' status=%s exit=%d time=%ldms out=%zuB",
                cmdline,
                result ? exec_status_str(result->status) : "N/A",
                exit_code,
                elapsed_ms,
                used);
        }
    }

    // Log failure details if command failed
    if (exit_code != 0 && log_enabled) {
        int quiet = (opts && opts->quiet);

        if (quiet) {
            LOG_EXEC_CHECK(LOG_DEBUG,
                "cmd='%s' failed status=%s exit=%d",
                cmdline,
                result ? exec_status_str(result->status) : "N/A",
                exit_code);

            if (resp && resp[0]) {
                LOG_EXEC_CHECK(LOG_DEBUG, "output: %.200s", resp);
            }
        } else {
            LOG_EXEC(LOG_WARN,
                "cmd='%s' failed status=%s exit=%d",
                cmdline,
                result ? exec_status_str(result->status) : "N/A",
                exit_code);

            if (resp && resp[0]) {
                LOG_EXEC(LOG_WARN, "output: %.200s", resp);
            }
        }
    }

    return 0;
}
