#ifndef EXEC_H
#define EXEC_H

#include <stddef.h>

// ===== defaults =====

#define EXEC_DEFAULT_TIMEOUT_MS 6000
#define EXEC_TIMEOUT_SEC (EXEC_DEFAULT_TIMEOUT_MS / 1000)

// ===== options =====

typedef struct {
    int timeout_ms;
    int capture_stderr;
    int log_output;
    int quiet; // 🔥 НОВОЕ
} exec_opts_t;

// ===== status =====

typedef enum {
    EXEC_OK = 0,
    EXEC_FORK_FAILED,
    EXEC_EXEC_FAILED,
    EXEC_EXIT_NONZERO,
    EXEC_SIGNAL_KILLED,
    EXEC_TIMEOUT,
    EXEC_PIPE_FAILED,
    EXEC_READ_FAILED
} exec_status_t;

// ===== result =====

typedef struct {
    exec_status_t status;

    int exit_code;
    int term_signal;

    int timed_out;   // legacy
    int signaled;    // legacy

    size_t stdout_len;
    size_t stderr_len;

    long duration_ms;
} exec_result_t;

// ===== helpers =====

const char* exec_status_str(exec_status_t s);

#define EXEC_IS_OK(r) ((r) && (r)->status == EXEC_OK)
#define EXEC_IS_TIMEOUT(r) ((r) && (r)->status == EXEC_TIMEOUT)
#define EXEC_IS_FATAL(r) ((r) && \
    ((r)->status == EXEC_EXEC_FAILED || \
     (r)->status == EXEC_FORK_FAILED || \
     (r)->status == EXEC_PIPE_FAILED))

int exec_success(const exec_result_t *r);

int exec_check_cmd(char *const argv[],
                   char *output,
                   size_t size,
                   const exec_opts_t *opts,
                   exec_result_t *res);

// ===== API =====

int exec_command(char *const argv[],
                 char *output,
                 size_t size,
                 const exec_opts_t *opts,
                 exec_result_t *result);

int exec_command_simple(char *const argv[],
                        char *output,
                        size_t size);

#endif // EXEC_H
