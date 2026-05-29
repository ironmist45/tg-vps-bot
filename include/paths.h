/**
 * tg-bot - Telegram bot for system administration
 * paths.h - Compile-time filesystem path constants
 *
 * Contains internal paths that are not user-configurable.
 * User-configurable paths (LOG_FILE, UPLOAD_DIR, SSH_KEYS_PATH, etc.)
 * are defined at runtime via the config file — see config.h.
 *
 * To relocate the bot data directory, change TG_DATA_DIR here
 * and rebuild. All derived paths update automatically.
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#ifndef PATHS_H
#define PATHS_H

/*
 * Bot data directory.
 * Holds internal runtime state files (offset, future state).
 * Must exist and be writable by the tg-bot user before starting:
 *   sudo mkdir -p /var/lib/tg-bot
 *   sudo chown tg-bot:tg-bot /var/lib/tg-bot
 */
#define TG_DATA_DIR     "/var/lib/tg-bot"

/*
 * Update offset persistence file.
 * Stores the last processed Telegram update_id across restarts.
 * Written atomically via TG_OFFSET_TMP + rename(2) + fsync(2).
 * If missing on startup, polling begins from offset 0.
 */
#define TG_OFFSET_FILE  TG_DATA_DIR "/offset.dat"

/*
 * Temporary file for atomic offset write.
 * Written first, then renamed to TG_OFFSET_FILE — guarantees
 * no partial write is ever visible as the live offset file.
 * Must be on the same filesystem as TG_OFFSET_FILE for rename(2)
 * to be atomic (same directory satisfies this).
 */
#define TG_OFFSET_TMP   TG_DATA_DIR "/offset.tmp"

#endif /* PATHS_H */
