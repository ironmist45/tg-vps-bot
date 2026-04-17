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

// ===== helpers =====

static void exec_result_fail(exec_result_t *r, int timed_out)
{
    if (!r) return;

    r->exit_code = -1;
    r->timed_out = timed_out;
    r->signaled = 0;
    r->bytes = 0;
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
            LOG_CMD(LOG_ERROR, "exec: invalid argv");
        }

        exec_result_fail(result, 0);
        return -1;
    }

    // ===== init output =====

    if (size > 0)
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
                LOG_CMD(LOG_WARN, "exec cmdline truncated");
            }
            break;
        }

        cmd_used += (size_t)written;
    }

    if (log_enabled) {
        LOG_CMD(LOG_DEBUG, "exec start: %s", cmdline);
    }

    // ===== pipe =====

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        if (log_enabled) {
            LOG_CMD(LOG_ERROR, "pipe failed: %s", cmdline);
        }

        exec_result_fail(result, 0);
        return -1;
    }

    // ===== fork =====

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    pid_t pid = fork();

    if (pid < 0) {
        if (log_enabled) {
            LOG_CMD(LOG_ERROR, "fork failed: %s", cmdline);
        }

        close(pipefd[0]);
        close(pipefd[1]);

        exec_result_fail(result, 0);
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

        exec_result_fail(result, 0);
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
                LOG_CMD(LOG_ERROR,
                        "exec timeout: %s (pid=%d)",
                        cmdline, pid);
            }

            kill(pid, SIGTERM);
            usleep(100000);
            kill(pid, SIGKILL);

            fclose(fp);
            waitpid(pid, NULL, 0);

            exec_result_fail(result, 1);
            return -1;
        }

        if (rv < 0) {
            if (errno == EINTR)
                continue;

            if (log_enabled) {
                LOG_CMD(LOG_ERROR, "select failed: %s", cmdline);
            }

            fclose(fp);
            waitpid(pid, NULL, 0);

            exec_result_fail(result, 0);
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
                LOG_CMD(LOG_ERROR,
                        "waitpid failed: %s (pid=%d)",
                        cmdline, pid);
            }

            exec_result_fail(result, 0);
            return -1;
        }

        usleep(100000);
        waited += 100;
    }

    if (!finished) {
        if (log_enabled) {
            LOG_CMD(LOG_WARN,
                    "exec timeout (%d ms): %s (pid=%d)",
                    timeout_ms, cmdline, pid);
        }

        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);

        exec_result_fail(result, 1);
        return -1;
    }

    // ===== result =====

    clock_gettime(CLOCK_MONOTONIC, &t_end);

    long elapsed_ms =
        (t_end.tv_sec - t_start.tv_sec) * 1000 +
        (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

    if (!WIFEXITED(status)) {
        if (log_enabled) {
            LOG_CMD(LOG_WARN,
                    "exec: %s -> terminated abnormally",
                    cmdline);
        }

        exec_result_fail(result, 0);
        return -1;
    }

    int exit_code = WEXITSTATUS(status);

    if (result) {
        result->exit_code = exit_code;
        result->timed_out = 0;
        result->signaled = WIFSIGNALED(status);
        result->bytes = (int)used;
        result->duration_ms = elapsed_ms;
    }

    if (log_enabled) {
        LOG_CMD(LOG_INFO,
                "exec: %s -> exit=%d, bytes=%zu, time=%ldms",
                cmdline, exit_code, used, elapsed_ms);
    }

    if (exit_code != 0 && log_enabled) {
        LOG_CMD(LOG_WARN,
                "exec failed: %s (exit=%d)",
                cmdline, exit_code);
    }

    return 0;
}
