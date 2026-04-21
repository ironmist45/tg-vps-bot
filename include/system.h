/**
 * tg-bot - Telegram bot for system administration
 * system.h - System information gathering (/status, /health, /ping)
 * MIT License - Copyright (c) 2026
 */

#ifndef SYSTEM_H
#define SYSTEM_H

#include <stddef.h>

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Get detailed system status with ASCII progress bars
 * 
 * Gathers comprehensive system information:
 *   - OS and hardware info
 *   - CPU load averages (1m, 5m, 15m)
 *   - Memory usage with visual bar
 *   - Disk usage with visual bar
 *   - System uptime
 *   - Active user count
 * 
 * Output format: Markdown code block with formatted table.
 * Used by /status command.
 * 
 * @param buf   Output buffer
 * @param size  Buffer size
 * @param req_id  16-bit request identifier for log correlation
 * @return      0 on success, -1 on error
 */
int system_get_status(char *buf, size_t size, unsigned short req_id);

/**
 * Get compact system status with emoji heat indicators
 * 
 * Shows essential metrics with visual indicators:
 *   🟢 normal, 🟡 warning, 🔴 critical
 * 
 * Metrics displayed:
 *   - CPU load (1m average)
 *   - Memory usage percentage
 *   - Disk usage percentage
 *   - System uptime
 * 
 * Output format: Compact Markdown (no code block).
 * Used by /health and /start commands.
 * 
 * @param buf   Output buffer
 * @param size  Buffer size
 * @param req_id  16-bit request identifier for log correlation
 * @return      0 on success, -1 on error
 */
int system_get_status_mini(char *buf, size_t size, unsigned short req_id);

/**
 * Get bot process uptime as formatted string
 * 
 * Format: "Xh Ym Zs" (e.g., "2h 30m 15s")
 * 
 * @param buf   Output buffer
 * @param size  Buffer size
 * @return      0 on success
 */
int system_get_uptime_str(char *buf, size_t size);

#endif // SYSTEM_H
