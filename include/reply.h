/**
 * tg-bot - Telegram bot for system administration
 * reply.h - Unified response formatting for command handlers
 * MIT License - Copyright (c) 2026
 */

#ifndef REPLY_H
#define REPLY_H

#include <stddef.h>
#include "commands.h"

// ============================================================================
// CORE REPLY API
// ============================================================================

/**
 * Send plain text response (no Markdown formatting)
 * 
 * Sets ctx->response_type to RESP_PLAIN.
 * 
 * @param ctx   Command context
 * @param text  Plain text response
 * @return      0 on success, -1 on error (buffer overflow, etc.)
 */
int reply_plain(command_ctx_t *ctx, const char *text);

/**
 * Send Markdown-formatted response
 * 
 * Text may contain Telegram MarkdownV2 formatting.
 * Caller is responsible for proper escaping of special characters.
 * Sets ctx->response_type to RESP_MARKDOWN.
 * 
 * @param ctx   Command context
 * @param text  Markdown-formatted response
 * @return      0 on success, -1 on error (buffer overflow, etc.)
 */
int reply_markdown(command_ctx_t *ctx, const char *text);

// ============================================================================
// CONVENIENCE HELPERS
// ============================================================================

/**
 * Send success response with optional custom text
 * 
 * Uses Markdown formatting. Defaults to "OK" if text is NULL.
 * 
 * @param ctx   Command context
 * @param text  Optional success message (NULL for "OK")
 * @return      0 on success, -1 on error
 */
int reply_ok(command_ctx_t *ctx, const char *text);

/**
 * Send error response with warning emoji prefix
 * 
 * Automatically prepends "⚠️ " to the error message.
 * Uses Markdown formatting.
 * 
 * @param ctx   Command context
 * @param text  Error message (NULL for generic "Error")
 * @return      0 on success, -1 on error
 */
int reply_error(command_ctx_t *ctx, const char *text);

#endif // REPLY_H
