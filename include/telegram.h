/**
 * tg-bot - Telegram bot for system administration
 * telegram.h - Telegram Bot API communication layer
 * MIT License - Copyright (c) 2026
 */

#ifndef TELEGRAM_H
#define TELEGRAM_H

#include <stddef.h>

// ============================================================================
// INITIALIZATION AND SHUTDOWN
// ============================================================================

/**
 * Initialize Telegram module
 * 
 * Sets up API token, base URL, and initializes libcurl.
 * Must be called once at program startup before any other telegram functions.
 * 
 * @param token  Telegram Bot API token (from @BotFather)
 * @return       0 on success, -1 on error (invalid or empty token)
 */
int telegram_init(const char *token);

/**
 * Shutdown Telegram module and cleanup resources
 * 
 * Performs libcurl global cleanup. Should be called during graceful shutdown.
 */
void telegram_shutdown(void);

// ============================================================================
// SENDING MESSAGES
// ============================================================================

/**
 * Send MarkdownV2 formatted message
 * 
 * Automatically escapes special characters and truncates messages
 * exceeding Telegram's 4096 character limit.
 * 
 * Special characters escaped: _ * [ ] ( ) ~ ` > # + - = | { } . !
 * 
 * @param chat_id  Target chat ID
 * @param text     Message text (may contain MarkdownV2 formatting)
 * @return         0 on success, -1 on error
 */
int telegram_send_message(long chat_id, const char *text);

/**
 * Send plain text message (no Markdown parsing)
 * 
 * Message is sent as-is without any formatting. Still subject to
 * Telegram's 4096 character limit (automatically truncated).
 * 
 * @param chat_id  Target chat ID
 * @param text     Plain text message
 * @return         0 on success, -1 on error
 */
int telegram_send_plain(long chat_id, const char *text);

// ============================================================================
// LONG POLLING
// ============================================================================

/**
 * Poll Telegram API for new messages (blocking)
 * 
 * Uses long polling with 25-second timeout. Processes all pending updates
 * and dispatches them to commands_handle().
 * 
 * Features:
 *   - Offset persistence (crash recovery)
 *   - Duplicate detection (in-memory)
 *   - Request ID generation for log correlation
 *   - Security checks (chat_id validation, input sanitization)
 * 
 * This is the main event source for the bot. Called from the main loop.
 * 
 * @return  0 on success (including timeout with no messages), -1 on error
 */
int telegram_poll(void);

// ============================================================================
// UTILITIES
// ============================================================================

/**
 * Get current poll cycle identifier
 * 
 * @return 16-bit poll_id (0000-FFFF), or 0 if not polling yet
 */
unsigned short telegram_get_poll_id(void);

/**
 * Get last saved update offset
 * @return Last offset value
 */
long telegram_get_last_offset(void);

#endif // TELEGRAM_H
