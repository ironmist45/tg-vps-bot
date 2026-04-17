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

#define EXEC_TIMEOUT_MS 6000
#define EXEC_TIMEOUT_SEC 5

int exec_command(char *const argv[],
                 char *resp,
                 size_t size)
{
    int pipefd[2];
    char cmdline[256] = {0};
    size_t cmd_used = 0;

    struct timespec t_start, t_end;

    // ===== BUILD CMDLINE =====
    for (int i = 0; argv[i]; i++) {
        int written = snprintf(cmdline + cmd_used,
                               sizeof(cmdline) - cmd_used,
                               "%s%s",
                               argv[i],
                               argv[i + 1] ? " " : "");

        if (written < 0 || (size_t)written >= sizeof(cmdline) - cmd_used) {
            LOG_CMD(LOG_WARN, "exec cmdline truncated");
            break;
        }

        cmd_used += (size_t)written;
    }

    LOG_CMD(LOG_DEBUG, "exec start: %s", cmdline);

    if (pipe(pipefd) < 0) {
        LOG_CMD(LOG_ERROR, "pipe failed: %s", cmdline);
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t_start);
    pid_t pid = fork();

    if (pid < 0) {
        LOG_CMD(LOG_ERROR, "fork failed: %s", cmdline);
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
            dup2(pipefd[1], STDERR_FILENO) < 0) {
            _exit(127);
        }

        close(pipefd[0]);
        close(pipefd[1]);

        execv("/usr/bin/sudo", argv);
        _exit(127);
    }

    // ===== PARENT =====
    close(pipefd[1]);

    resp[0] = '\0';
    size_t used = 0;

    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        close(pipefd[0]);
        return -1;
    }

    int fd = pipefd[0];

    while (1) {
        fd_set set;
        struct timeval timeout;

        FD_ZERO(&set);
        FD_SET(fd, &set);

        timeout.tv_sec = EXEC_TIMEOUT_SEC;
        timeout.tv_usec = 0;

        int rv = select(fd + 1, &set, NULL, NULL, &timeout);

        if (rv == 0) {
            LOG_CMD(LOG_ERROR,
                    "exec timeout: %s (pid=%d)",
                    cmdline, pid);
            kill(pid, SIGKILL);
            fclose(fp);
            waitpid(pid, NULL, 0);
            return -1;
        }

        if (rv < 0) {
            if (errno == EINTR)
                continue;

            LOG_CMD(LOG_ERROR, "select failed: %s", cmdline);
            fclose(fp);
            waitpid(pid, NULL, 0);
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

    int status = 0;
    int waited = 0;
    int finished = 0;

    while (waited < EXEC_TIMEOUT_MS) {
        pid_t rc = waitpid(pid, &status, WNOHANG);

        if (rc == pid) {
            finished = 1;
            break;
        }

        if (rc == -1) {
            LOG_CMD(LOG_ERROR,
                    "waitpid failed: %s (pid=%d)",
                    cmdline, pid);
            break;
        }

        usleep(100000);
        waited += 100;
    }

    if (!finished) {
        LOG_CMD(LOG_WARN,
                "exec timeout (%d ms): %s (pid=%d)",
                EXEC_TIMEOUT_MS, cmdline, pid);

        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        return -1;
    }

    if (!WIFEXITED(status)) {
        LOG_CMD(LOG_WARN,
                "exec: %s -> terminated abnormally",
                cmdline);
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t_end);

    int exit_code = WEXITSTATUS(status);

    long elapsed_ms =
        (t_end.tv_sec - t_start.tv_sec) * 1000 +
        (t_end.tv_nsec - t_start.tv_nsec) / 1000000;

    LOG_CMD(LOG_INFO,
            "exec: %s -> exit=%d, bytes=%zu, time=%ldms",
            cmdline, exit_code, used, elapsed_ms);

    if (exit_code != 0) {
        LOG_CMD(LOG_WARN,
                "exec failed: %s (exit=%d)",
                cmdline, exit_code);
    }

    return 0;
}
