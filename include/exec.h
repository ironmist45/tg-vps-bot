#ifndef EXEC_H
#define EXEC_H

#include <stddef.h>

// ===== defaults =====

#define EXEC_TIMEOUT_SEC (EXEC_DEFAULT_TIMEOUT_MS / 1000)

// ===== options =====

typedef struct {
    int timeout_ms;        // timeout (ms), 0 = default
    int capture_stderr;    // 1 = merge stderr into stdout
    int log_output;        // 1 = log command execution
} exec_opts_t;

// ===== result =====

typedef struct {
    int exit_code;         // exit code (если есть)
    int timed_out;         // 1 если был timeout
    int signaled;          // 1 если убит сигналом
    int bytes;             // сколько байт записано
    long duration_ms;      // время выполнения
} exec_result_t;

// ===== API =====

// 🔹 основной вызов
int exec_command(char *const argv[],
                 char *output,
                 size_t size,
                 const exec_opts_t *opts,
                 exec_result_t *result);

// 🔹 упрощённый (backward-compatible)
int exec_command_simple(char *const argv[],
                        char *output,
                        size_t size);

#endif // EXEC_H
