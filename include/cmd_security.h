/**
 * tg-bot - Telegram bot for system administration
 * cmd_security.h - Security command handlers (/fail2ban, /sshkeys)
 * MIT License - Copyright (c) 2026 ironmist45
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
 * All commands are executed via sudo with the configured f2b_wrapper_path.
 * IP addresses are validated using is_safe_ip() before processing.
 * Status output is returned as plain text.
 *
 * @param ctx  Command context
 * @return     0 on success, -1 on error
 */
int cmd_fail2ban_v2(command_ctx_t *ctx);

/**
 * Show SSH authorized keys from configured path (V2 handler)
 *
 * Reads SSH_KEYS_PATH via 'sudo cat' and displays key type and comment
 * for each entry — the key itself is not shown.
 *
 * If SSH_KEYS_PATH is not set in config, returns a plain-text hint
 * explaining how to enable the feature. The command is always registered
 * in the command table regardless of config state.
 *
 * Requires sudoers entry:
 *   tg-bot ALL=(ALL) NOPASSWD: /bin/cat /home/user/.ssh/authorized_keys
 *
 * @param ctx  Command context
 * @return     0 on success, -1 on error
 */
int cmd_sshkeys_v2(command_ctx_t *ctx);

#endif /* CMD_SECURITY_H */
