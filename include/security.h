/**
 * tg-bot - Telegram bot for system administration
 * security.h - Access control and token-based confirmation
 * MIT License - Copyright (c) 2026
 */

#ifndef SECURITY_H
#define SECURITY_H

#include <stdio.h>
#include <stddef.h>

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * Initialize security module
 * 
 * Generates runtime salt for token generation. Must be called once
 * at program startup before any other security functions.
 */
void security_init(void);

// ============================================================================
// CONFIGURATION
// ============================================================================

/**
 * Set the allowed Telegram chat ID (single-user mode)
 * 
 * Only messages from this chat ID will be processed.
 * 
 * @param chat_id  Telegram chat ID of the authorized user
 */
void security_set_allowed_chat(long chat_id);

/**
 * Set token time-to-live for reboot confirmation
 * 
 * @param ttl  Token validity period in seconds (1-3600)
 */
void security_set_token_ttl(int ttl);

// ============================================================================
// ACCESS CONTROL
// ============================================================================

/**
 * Check if chat ID matches the allowed user
 * 
 * @param chat_id  Telegram chat ID to validate
 * @return         1 if allowed, 0 otherwise
 */
int security_is_allowed_chat(long chat_id);

/**
 * Check access for a specific command
 * 
 * Verifies that the requesting chat ID is authorized and logs the result.
 * 
 * @param chat_id  Telegram chat ID making the request
 * @param cmd      Command being executed (for logging)
 * @param req_id   16-bit request identifier (for log correlation)
 * @return         0 if access granted, -1 if denied
 */
int security_check_access(long chat_id, const char *cmd, unsigned short req_id);

/**
 * Validate text input for safety (no binary/control characters)
 * 
 * Allows only printable characters, newlines, carriage returns, and tabs.
 * Rejects strings containing binary data or other control characters.
 * 
 * @param text  Input string to validate
 * @return      0 if valid, -1 if invalid or too long
 */
int security_validate_text(const char *text);

/**
 * Apply rate limiting to prevent command spam
 * 
 * Allows up to 5 commands per second before throttling.
 * 
 * @return  0 if request allowed, -1 if rate limit exceeded
 */
int security_rate_limit(void);

// ============================================================================
// REBOOT TOKENS (STATELESS)
// ============================================================================

/**
 * Generate a new reboot confirmation token
 * 
 * Token is stateless: derived from chat_id, current time, and salts.
 * Stored internally for replay protection (single-use).
 * 
 * @param chat_id  Authorized chat ID requesting the token
 * @param req_id   16-bit request identifier for log correlation
 * @return         6-digit token (000000-999999)
 */
int security_generate_reboot_token(long chat_id, unsigned short req_id);

/**
 * Validate a reboot confirmation token
 * 
 * Checks token against time windows within TTL. Also enforces:
 *   - Token format (6 digits)
 *   - Bruteforce block status (after 5 failures)
 *   - Replay protection (single-use)
 * 
 * @param chat_id      Authorized chat ID
 * @param token        Token to validate (provided by user)
 * @param req_id       16-bit request identifier for log correlation
 * @return             0 if valid, -1 if invalid/expired/replayed
 */
int security_validate_reboot_token(long chat_id, int token, unsigned short req_id);

#endif // SECURITY_H
