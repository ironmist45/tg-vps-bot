/**
 * tg-bot - Telegram bot for system administration
 * users.h - Active user session monitoring
 * MIT License - Copyright (c) 2026 ironmist45
 */

#ifndef USERS_H
#define USERS_H

#include <stddef.h>

// ============================================================================
// HIGH-LEVEL API
// ============================================================================

/**
 * Get formatted user list for Telegram response
 * 
 * Wraps users_get_logged() with a header containing total user count.
 * Used by the /users command.
 * 
 * Output format (Markdown):
 *   *👤 ACTIVE USERS: N*
 *   
 *   • 👤 `username`
 *     🖥 `tty`
 *     🌐 host
 *     ⏱ login_time
 * 
 * If no active sessions: "_No active sessions_"
 * 
 * @param buffer  Output buffer for complete response
 * @param size    Size of output buffer
 * @param req_id  16-bit request identifier for log correlation
 * @return        0 on success, -1 on error
 */
int users_get(char *buffer, size_t size, unsigned short req_id);

#endif // USERS_H
