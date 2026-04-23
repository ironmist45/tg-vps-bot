/**
 * tg-bot - Telegram bot for system administration
 * telegram_offset.h - Offset persistence API
 * MIT License - Copyright (c) 2026
 */

#ifndef TELEGRAM_OFFSET_H
#define TELEGRAM_OFFSET_H

/**
 * Initialize offset module
 */
void telegram_offset_init(void);

/**
 * Load last processed update_id from disk
 * @return Last processed update_id, or 0 if no saved state
 */
long telegram_offset_load(void);

/**
 * Save current update_id to disk (atomic via rename)
 * @param offset Update_id to save
 */
void telegram_offset_save(long offset);

/**
 * Get last saved offset (for shutdown logging)
 * @return Last offset value
 */
long telegram_offset_get_last(void);

#endif // TELEGRAM_OFFSET_H
