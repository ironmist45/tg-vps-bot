/**
 * tg-bot - Telegram bot for system administration
 * users.h - Active user session monitoring
 * MIT License - Copyright (c) 2026
 */

#ifndef USERS_H
#define USERS_H

#include <stddef.h>

// ============================================================================
// LOW-LEVEL API
// ============================================================================

/**
 * Retrieve raw list of active user sessions from utmp
 * 
 * Enumerates all USER_PROCESS entries from the utmp database.
 * Each session is formatted with user, TTY, host, and login time.
 * 
 * Output format (per session):
 *   • 👤 `username`
 *     🖥 `tty`
 *     🌐 host
 *     ⏱ login_time
 * 
 * @param buffer  Output buffer for formatted session list
 * @param size    Size of output buffer
 * @return        Number of active sessions, or -1 on error
 */
int users_get_logged(char *buffer, size_t size);

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
 * @return        0 on success, -1 on error
 */
int users_get(char *buffer, size_t size, unsigned short req_id);

#endif // USERS_H
