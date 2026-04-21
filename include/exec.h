/**
 * tg-bot - Telegram bot for system administration
 * exec.h - External command execution with timeout and capture
 * MIT License - Copyright (c) 2026
 */

#ifndef EXEC_H
#define EXEC_H

#include <stddef.h>

// ============================================================================
// DEFAULTS
// ============================================================================

#define EXEC_DEFAULT_TIMEOUT_MS 6000        // Default timeout in milliseconds
#define EXEC_TIMEOUT_SEC (EXEC_DEFAULT_TIMEOUT_MS / 1000)  // Timeout in seconds

// ============================================================================
// EXECUTION OPTIONS
// ============================================================================

/**
 * Command execution options
 */
typedef struct {
    int timeout_ms;      // Timeout in milliseconds (0 = default)
    int capture_stderr;  // Capture stderr along with stdout (1 = yes, 0 = no)
    int log_output;      // Log command output (1 = yes, 0 = no)
    int quiet;           // Suppress non-error logging (1 = quiet, 0 = verbose)
} exec_opts_t;

// ============================================================================
// EXECUTION STATUS
// ============================================================================

/**
 * Command execution status codes
 */
typedef enum {
    EXEC_OK = 0,           // Command executed successfully (exit code 0)
    EXEC_FORK_FAILED,      // fork() system call failed
    EXEC_EXEC_FAILED,      // execv() system call failed (e.g., command not found)
    EXEC_EXIT_NONZERO,     // Command executed but returned non-zero exit code
    EXEC_SIGNAL_KILLED,    // Command terminated by signal
    EXEC_TIMEOUT,          // Command exceeded timeout and was killed
    EXEC_PIPE_FAILED,      // pipe() system call failed
    EXEC_READ_FAILED       // Failed to read command output
} exec_status_t;

// ============================================================================
// EXECUTION RESULT
// ============================================================================

/**
 * Detailed command execution result
 */
typedef struct {
    exec_status_t status;  // Overall execution status
    
    int exit_code;         // Process exit code (0-255, -1 if not applicable)
    int term_signal;       // Termination signal (if killed by signal)
    
    int timed_out;         // Legacy: 1 if timeout occurred
    int signaled;          // Legacy: 1 if killed by signal
    
    size_t stdout_len;     // Length of captured stdout
    size_t stderr_len;     // Length of captured stderr (if capture_stderr enabled)
    
    long duration_ms;      // Total execution time in milliseconds
} exec_result_t;

// ============================================================================
// STATUS HELPERS
// ============================================================================

/**
 * Convert execution status to human-readable string
 * 
 * @param s  Status code
 * @return   String representation (e.g., "OK", "TIMEOUT")
 */
const char* exec_status_str(exec_status_t s);

/**
 * Check if result indicates successful execution
 * 
 * @param r  Execution result
 * @return   1 if status == EXEC_OK and exit_code == 0, 0 otherwise
 */
int exec_success(const exec_result_t *r);

// Status check macros
#define EXEC_IS_OK(r)      ((r) && (r)->status == EXEC_OK)
#define EXEC_IS_TIMEOUT(r) ((r) && (r)->status == EXEC_TIMEOUT)
#define EXEC_IS_FATAL(r)   ((r) && \
    ((r)->status == EXEC_EXEC_FAILED || \
     (r)->status == EXEC_FORK_FAILED || \
     (r)->status == EXEC_PIPE_FAILED))

// ============================================================================
// CONVENIENCE FUNCTIONS
// ============================================================================

/**
 * Execute command and check if it succeeded
 * 
 * Convenience wrapper combining exec_command() and exec_success().
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
                   exec_result_t *res);

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Execute external command with full options
 * 
 * Commands are executed via /usr/bin/sudo for privilege escalation.
 * 
 * Features:
 *   - Timeout protection (SIGTERM → SIGKILL escalation)
 *   - Stdout/stderr capture
 *   - Comprehensive status reporting
 *   - Execution time measurement
 * 
 * @param argv    Command argument array (NULL-terminated)
 * @param output  Buffer for captured output (may be NULL)
 * @param size    Size of output buffer
 * @param opts    Execution options (may be NULL for defaults)
 * @param result  Result structure to populate (may be NULL)
 * @return        0 on success (check result->status for details), -1 on error
 */
int exec_command(char *const argv[],
                 char *output,
                 size_t size,
                 const exec_opts_t *opts,
                 exec_result_t *result);

/**
 * Execute external command with default options
 * 
 * Simplified interface for basic use cases.
 * 
 * @param argv    Command argument array (NULL-terminated)
 * @param output  Buffer for captured output
 * @param size    Size of output buffer
 * @return        0 on success, -1 on failure
 */
int exec_command_simple(char *const argv[],
                        char *output,
                        size_t size);

#endif // EXEC_H
