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

// ==== exec status ====

const char* exec_status_str(exec_status_t s) {
    switch (s) {
        case EXEC_OK: return "OK";
        case EXEC_FORK_FAILED: return "FORK_FAILED";
        case EXEC_EXEC_FAILED: return "EXEC_FAILED";
        case EXEC_EXIT_NONZERO: return "EXIT_NONZERO";
        case EXEC_SIGNAL_KILLED: return "SIGNALED";
        case EXEC_TIMEOUT: return "TIMEOUT";
        case EXEC_PIPE_FAILED: return "PIPE_FAILED";
        case EXEC_READ_FAILED: return "READ_FAILED";
        default: return "UNKNOWN";
    }
}

// ===== helpers =====

static void exec_result_fail(exec_result_t *r,
                            exec_status_t status)
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

// ===== forward =====

static int exec_command_internal(char *const argv[],
                                 char *resp,
                                 size_t size,
                                 const exec_opts_t *opts,
                                 exec_result_t *result);

// ===== public API =====

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

int exec_command_simple(char *const argv[],
                        char *output,
                        size_t size)
{
    return exec_command(argv, output, size, NULL, NULL);
}

// ===== internal implementation =====

static int exec_command_internal(char *const argv[],
                                 char *resp,
                                 size_t size,
                                 const exec_opts_t *opts,
                                 exec_result_t *result)
{
    int capture_stderr = 1;
    int log_enabled = 1;

    if (opts) {
        if (opts->capture_stderr == 0)
            capture_stderr = 0;

        if (opts->log_output == 0)
            log_enabled = 0;
    }

    // ===== validate argv =====

    if (!argv || !argv[0]) {
        if (log_enabled) {
            LOG_EXEC(LOG_ERROR, "invalid argv (empty command)");
        }

        exec_result_fail(result, EXEC_EXEC_FAILED);
        return -1;
    }

    // ===== init output =====

    if (resp && size > 0)
        resp[0] = '\0';

    // ===== build cmdline =====

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

    // ===== pipe =====

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        if (log_enabled) {
            LOG_EXEC(LOG_ERROR, "pipe failed cmd='%s'", cmdline);
        }

        exec_result_fail(result, EXEC_PIPE_FAILED);
        return -1;
    }

    // ===== fork =====

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

        long elapsed_ms =
        (t_end.tv_sec - t_start.tv_sec) * 1000 +
        (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

        if (result) result->duration_ms = elapsed_ms;

        exec_result_fail(result, EXEC_FORK_FAILED);
        return -1;
    }

    // ===== child =====

    if (pid == 0) {
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
            (capture_stderr && dup2(pipefd[1], STDERR_FILENO) < 0)) {
            _exit(127);

        }

        close(pipefd[0]);
        close(pipefd[1]);

        execv("/usr/bin/sudo", argv);
        _exit(127);
    }

    // ===== parent =====

    close(pipefd[1]);

    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        close(pipefd[0]);

        clock_gettime(CLOCK_MONOTONIC, &t_end);

        long elapsed_ms =
        (t_end.tv_sec - t_start.tv_sec) * 1000 +
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

    // ===== read loop =====

    while (1) {
        fd_set set;
        struct timeval timeout;

        FD_ZERO(&set);
        FD_SET(fd, &set);

        timeout.tv_sec  = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;

        int rv = select(fd + 1, &set, NULL, NULL, &timeout);

        if (rv == 0) {
            if (log_enabled) {
                LOG_EXEC(LOG_ERROR,
                        "timeout cmd='%s' pid=%d",
                        cmdline, pid);
            }

            kill(pid, SIGTERM);
            usleep(100000);
            kill(pid, SIGKILL);

            fclose(fp);
            waitpid(pid, NULL, 0);

            clock_gettime(CLOCK_MONOTONIC, &t_end);

            long elapsed_ms =
            (t_end.tv_sec - t_start.tv_sec) * 1000 +
            (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

            if (result) result->duration_ms = elapsed_ms;

            exec_result_fail(result, EXEC_TIMEOUT);
            return -1;
        }

        if (rv < 0) {
            if (errno == EINTR)
                continue;

            if (log_enabled) {
                LOG_EXEC(LOG_ERROR, "select failed cmd='%s'", cmdline);
            }

            fclose(fp);
            waitpid(pid, NULL, 0);

            clock_gettime(CLOCK_MONOTONIC, &t_end);

            long elapsed_ms =
            (t_end.tv_sec - t_start.tv_sec) * 1000 +
            (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

            if (result) result->duration_ms = elapsed_ms;

            exec_result_fail(result, EXEC_READ_FAILED);
            return -1;
        }

        if (used >= size - 1)
            break;

        if (fgets(resp + used, size - used, fp) == NULL)
            break;

        size_t len = strlen(resp + used);
        used += len;

        if (used >= size - 1) {
            strncat(resp, "\n...truncated...", size - strlen(resp) - 1);
            break;
        }
    }

    fclose(fp);

    // ===== wait =====

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
                LOG_EXEC(LOG_ERROR,
                        "waitpid failed cmd='%s' pid=%d",
                        cmdline, pid);
            }

            clock_gettime(CLOCK_MONOTONIC, &t_end);

            long elapsed_ms =
            (t_end.tv_sec - t_start.tv_sec) * 1000 +
            (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

            if (result) result->duration_ms = elapsed_ms;
            
            exec_result_fail(result, EXEC_READ_FAILED);
            return -1;
        }

        usleep(100000);
        waited += 100;
    }

    if (!finished) {
        if (log_enabled) {
            LOG_EXEC(LOG_WARN,
                    "exec timeout (%d ms): %s (pid=%d)",
                    timeout_ms, cmdline, pid);
        }

        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);

        clock_gettime(CLOCK_MONOTONIC, &t_end);

        long elapsed_ms =
        (t_end.tv_sec - t_start.tv_sec) * 1000 +
        (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

        if (result) result->duration_ms = elapsed_ms;

        exec_result_fail(result, EXEC_TIMEOUT);
        return -1;
    }

    // ===== result =====

    clock_gettime(CLOCK_MONOTONIC, &t_end);

    long elapsed_ms =
        (t_end.tv_sec - t_start.tv_sec) * 1000 +
        (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

    if (!WIFEXITED(status)) {
        if (result) {
            result->status = EXEC_SIGNAL_KILLED;
            result->term_signal = WTERMSIG(status);
        }

        LOG_EXEC(LOG_WARN,
            "cmd='%s' killed by signal=%d",
            cmdline,
            WTERMSIG(status));

        return -1;
    }
    
    int exit_code = WEXITSTATUS(status);

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

    if (log_enabled) {

        int quiet = (opts && opts->quiet);

        if (quiet) {
            LOG_EXEC_CHECK(LOG_DEBUG,
                "cmd='%s' status=%s exit=%d time=%ldms out=%zuB",
                cmdline,
                result ? exec_status_str(result->status) : "N/A",
                exit_code,
                elapsed_ms,
                used
            );
        } else {
            LOG_EXEC(LOG_INFO,
                "cmd='%s' status=%s exit=%d time=%ldms out=%zuB",
                cmdline,
                result ? exec_status_str(result->status) : "N/A",
                exit_code,
                elapsed_ms,
                used
            );
        }
    }

    if (exit_code != 0 && log_enabled) {

        int quiet = (opts && opts->quiet);

        if (quiet) {
            LOG_EXEC_CHECK(LOG_DEBUG,
                "cmd='%s' failed status=%s exit=%d",
                cmdline,
                result ? exec_status_str(result->status) : "N/A",
                exit_code
            );

            if (resp && resp[0]) {
                LOG_EXEC_CHECK(LOG_DEBUG,
                    "output: %.200s",
                    resp
                );
            }

        } else {
            LOG_EXEC(LOG_WARN,
                "cmd='%s' failed status=%s exit=%d",
                cmdline,
                result ? exec_status_str(result->status) : "N/A",
                exit_code
            );

            if (resp && resp[0]) {
                LOG_EXEC(LOG_WARN,
                    "output: %.200s",
                    resp
                );
            }
        }
    }

    return 0;
}
