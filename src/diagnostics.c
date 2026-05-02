/**
 * tg-bot - Telegram bot for system administration
 *
 * diagnostics.c - Runtime diagnostics and health monitoring
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "diagnostics.h"
#include "logger.h"
#include "utils.h"

#include <time.h>

// ============================================================================
// INTERNAL STATE
// ============================================================================

/* Timestamp set by diag_loop_start(), consumed by diag_loop_end() */
static struct timespec g_loop_start = {0, 0};

/* Count of slow iterations since process start — for log context */
static unsigned long g_slow_iter_count = 0;

// ============================================================================
// LOOP ITERATION TIMING
// ============================================================================

/**
 * Record the start timestamp of a main loop iteration.
 */
void diag_loop_start(void) {
    clock_gettime(CLOCK_MONOTONIC, &g_loop_start);
}

/**
 * Record the end timestamp and evaluate iteration duration.
 *
 * Logs a warning if the iteration exceeded DIAG_LOOP_WARN_MS.
 * The warning includes the duration and a running count of slow
 * iterations to help distinguish one-off delays from a pattern.
 */
void diag_loop_end(void) {
    /*
     * Guard against diag_loop_end() called without diag_loop_start().
     * A zero timespec means the start was never recorded.
     */
    if (g_loop_start.tv_sec == 0 && g_loop_start.tv_nsec == 0)
        return;

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);

    long ms = elapsed_ms(g_loop_start, end);

    if (ms >= DIAG_LOOP_WARN_MS) {
        g_slow_iter_count++;
        LOG_SYS(LOG_WARN,
            "Main loop iteration took %ld ms"
            " (threshold: %d ms, slow count: %lu)",
            ms, DIAG_LOOP_WARN_MS, g_slow_iter_count);
    }

    /* Reset so a missed diag_loop_start() is detectable */
    g_loop_start.tv_sec  = 0;
    g_loop_start.tv_nsec = 0;
}
