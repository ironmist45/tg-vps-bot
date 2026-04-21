/**
 * tg-bot - Telegram bot for system administration
 * logs.h - Systemd journal log retrieval and filtering
 * MIT License - Copyright (c) 2026
 */

#ifndef LOGS_H
#define LOGS_H

#include <stddef.h>

// ============================================================================
// TYPES
// ============================================================================

/**
 * Log retrieval result statistics
 */
typedef struct {
    int matched;  // Number of lines that passed the filter
    int raw;      // Total number of lines scanned
} logs_result_t;

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Retrieve and filter systemd journal logs
 * 
 * Executes journalctl with appropriate arguments and formats the output
 * for Telegram display. Supports service aliases, line limits, and
 * multi-keyword filtering.
 * 
 * Usage:
 *   /logs                       - List available services (when service == "")
 *   /logs <service>             - Last 30 lines
 *   /logs <service> <N>         - Last N lines (max 200)
 *   /logs <service> <filter>    - Filter results (e.g., "error", "auth", "brute")
 * 
 * Allowed services: ssh, mtg, shadowsocks
 * 
 * Filter keywords:
 *   - Literal: any string (case-insensitive substring match)
 *   - Semantic groups: error, auth, brute, ip, session, pam
 *   - Multiple keywords: space-separated (AND logic)
 * 
 * @param service  Command arguments string (may be empty for service list)
 * @param buffer   Output buffer for formatted response
 * @param size     Size of output buffer
 * @return         0 on success, -1 on error
 */
int logs_get(const char *service, char *buffer, size_t size);

#endif // LOGS_H
