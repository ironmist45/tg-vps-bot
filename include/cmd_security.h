/**
 * tg-bot - Telegram bot for system administration
 * cmd_security.h - Security command handlers (/fail2ban)
 * MIT License - Copyright (c) 2026
 */

#ifndef CMD_SECURITY_H
#define CMD_SECURITY_H

#include "commands.h"

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Manage Fail2Ban via f2b-wrapper (V2 handler)
 * 
 * Subcommands:
 *   /fail2ban status [jail]  - Show ban status (optionally for specific jail)
 *   /fail2ban ban <ip>       - Ban IP address in sshd jail
 *   /fail2ban unban <ip>     - Unban IP address from sshd jail
 * 
 * All commands are executed via sudo with /usr/local/bin/f2b-wrapper.
 * IP addresses are validated using is_safe_ip() before processing.
 * Status output is returned in a plain text code block.
 * 
 * @param ctx  Command context
 * @return     0 on success, -1 on error
 */
int cmd_fail2ban_v2(command_ctx_t *ctx);

#endif // CMD_SECURITY_H
