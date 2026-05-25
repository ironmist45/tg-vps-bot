/**
 * tg-bot - Telegram bot for system administration
 * cmd_upload.h - File upload command handlers (/files)
 * MIT License - Copyright (c) 2026 ironmist45
 */
#ifndef CMD_UPLOAD_H
#define CMD_UPLOAD_H

#include "commands.h"

/**
 * Handle an incoming file sent to the bot.
 *
 * Called by telegram_poll.c when an update contains a document
 * (file_id != NULL). Not a slash command — triggered automatically
 * when the user sends a file.
 *
 * Flow:
 *   1. Send "📥 Uploading..." acknowledgement
 *   2. Call upload_receive_file() to download and save the file
 *   3. Send "✅ Saved: <filename> (<size>)" or error message
 *
 * @param ctx  Command context (file_id and file_name from update)
 * @return     0 on success, -1 on error
 */
int cmd_handle_upload(command_ctx_t *ctx);

/**
 * /files — list files in the upload directory
 *
 * Shows all files saved in g_cfg.upload_dir with their sizes.
 * Hidden from /help if upload_dir is not configured.
 *
 * @param ctx  Command context
 * @return     0 on success, -1 on error
 */
int cmd_files_v2(command_ctx_t *ctx);

#endif // CMD_UPLOAD_H
