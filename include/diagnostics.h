/**
 * tg-bot - Telegram bot for system administration
 *
 * diagnostics.h - Runtime diagnostics and health monitoring
 *
 * Provides lightweight instrumentation for the main event loop
 * and other critical paths. Designed to be extended incrementally
 * as new diagnostic needs arise.
 *
 * Current functionality:
 *   - Main loop iteration timing (diag_loop_start / diag_loop_end)
 *     Logs a warning if a single iteration exceeds DIAG_LOOP_WARN_MS,
 *     which may indicate a hang in telegram_poll() or command handling.
 *
 * Planned extensions:
 *   - diag_check_services()  — alert on service status changes
 *   - diag_poll_error()      — track consecutive polling failures
 *   - diag_cmd_timing()      — per-command response time statistics
 *   - diag_heartbeat()       — periodic system health snapshot
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <time.h>

// ============================================================================
// CONSTANTS
// ============================================================================

/*
 * Warn if a single main loop iteration exceeds this threshold.
 *
 * One normal iteration: poll timeout (25s) + usleep(200ms) ≈ 25.2s.
 * A value of 35 000 ms gives ~10s headroom above the expected maximum
 * before we consider the loop potentially hung.
 * Must be less than WatchdogSec (60s) so the warning appears in the
 * log before systemd kills the process.
 */
#define DIAG_LOOP_WARN_MS  35000

// ============================================================================
// LOOP ITERATION TIMING
// ============================================================================

/**
 * Record the start timestamp of a main loop iteration.
 *
 * Call once at the top of the while(1) loop, before any work is done.
 * Uses CLOCK_MONOTONIC so wall-clock adjustments do not affect results.
 */
void diag_loop_start(void);

/**
 * Record the end timestamp and evaluate iteration duration.
 *
 * Call once at the bottom of the while(1) loop, after all work is done.
 * Logs a WARN message if the iteration exceeded DIAG_LOOP_WARN_MS.
 *
 * Log format on slow iteration:
 *   [WARN] [DIAG] Main loop iteration took Nms (threshold: Xms)
 */
void diag_loop_end(void);

#endif /* DIAGNOSTICS_H */
