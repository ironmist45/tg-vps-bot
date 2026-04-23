/**
 * tg-bot - Telegram bot for system administration
 * telegram_poll.h - Long polling API
 * MIT License - Copyright (c) 2026
 */

#ifndef TELEGRAM_POLL_H
#define TELEGRAM_POLL_H

/**
 * Poll Telegram API for new messages (non-blocking with shutdown check)
 * @return 0 on success, -1 on error or aborted by shutdown
 */
int telegram_poll(void);

/**
 * Get current poll cycle identifier
 * @return 16-bit poll_id (0000-FFFF), or 0 if not polling yet
 */
unsigned short telegram_get_poll_id(void);

#endif // TELEGRAM_POLL_H
