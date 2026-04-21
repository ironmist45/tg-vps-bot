/**
 * tg-bot - Telegram bot for system administration
 * 
 * reply.c - Unified response formatting for command handlers
 * 
 * Provides a clean API for command handlers to send responses.
 * Automatically handles:
 *   - Response type selection (Markdown vs plain text)
 *   - Buffer overflow protection
 *   - Consistent logging of responses
 *   - Pre-formatted OK and error messages
 * 
 * All response functions use the command_ctx_t structure which contains
 * the response buffer, size limit, and response type pointer.
 * 
 * MIT License
 * 
 * Copyright (c) 2026
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "reply.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>

// ============================================================================
// INTERNAL: CORE RESPONSE WRITER
// ============================================================================

/**
 * Write response to command context buffer
 * 
 * This is the internal function used by all public reply functions.
 * It handles:
 *   - NULL pointer validation
 *   - Buffer overflow protection
 *   - Response type assignment
 *   - Consistent logging of all responses
 * 
 * @param ctx   Command context containing response buffer
 * @param type  Response type (Markdown or plain text)
 * @param text  Response text to write
 * @return      0 on success, -1 on error (NULL context, overflow, etc.)
 */
static int reply_write(command_ctx_t *ctx,
                       response_type_t type,
                       const char *text)
{
    // Validate context and buffer
    if (!ctx || !ctx->response || ctx->resp_size == 0) {
        return -1;
    }

    // Empty text is allowed (treat as empty string)
    if (!text) {
        text = "";
    }

    // Write response with truncation protection
    int written = snprintf(ctx->response,
                           ctx->resp_size,
                           "%s",
                           text);

    if (written < 0 || (size_t)written >= ctx->resp_size) {
        // Ensure null termination on overflow
        ctx->response[ctx->resp_size - 1] = '\0';
        return -1;
    }

    // Set response type for Telegram formatting
    if (ctx->resp_type) {
        *(ctx->resp_type) = type;
    }

    // Log response for debugging and audit
    LOG_CTX(LOG_CMD, ctx, LOG_DEBUG,
        "RES %s (%zu chars)",
        ctx->raw_text ? ctx->raw_text : "NULL",
        strlen(ctx->response));
  
    return 0;
}

// ============================================================================
// PUBLIC API: RESPONSE FORMATTING
// ============================================================================

/**
 * Send plain text response (no Markdown formatting)
 * 
 * @param ctx   Command context
 * @param text  Plain text response
 * @return      0 on success, -1 on error
 */
int reply_plain(command_ctx_t *ctx, const char *text)
{
    return reply_write(ctx, RESP_PLAIN, text);
}

/**
 * Send Markdown-formatted response
 * 
 * Text may contain Telegram MarkdownV2 formatting.
 * Caller is responsible for proper escaping of special characters.
 * 
 * @param ctx   Command context
 * @param text  Markdown-formatted response
 * @return      0 on success, -1 on error
 */
int reply_markdown(command_ctx_t *ctx, const char *text)
{
    return reply_write(ctx, RESP_MARKDOWN, text);
}

/**
 * Send success response with optional custom text
 * 
 * Defaults to "OK" if text is NULL.
 * Uses Markdown formatting (no special prefix).
 * 
 * @param ctx   Command context
 * @param text  Optional success message (NULL for "OK")
 * @return      0 on success, -1 on error
 */
int reply_ok(command_ctx_t *ctx, const char *text)
{
    return reply_markdown(ctx, text ? text : "OK");
}

/**
 * Send error response with warning emoji prefix
 * 
 * Automatically prepends "⚠️ " to the error message.
 * Uses Markdown formatting for the response.
 * 
 * @param ctx   Command context
 * @param text  Error message (NULL for generic "Error")
 * @return      0 on success, -1 on error
 */
int reply_error(command_ctx_t *ctx, const char *text)
{
    char buffer[256];

    if (!text) {
        text = "Error";
    }

    // Format error message with warning prefix
    int written = snprintf(buffer, sizeof(buffer),
                           "⚠️ %s", text);

    if (written < 0 || (size_t)written >= sizeof(buffer)) {
        // Fallback to safe generic message on overflow
        return reply_markdown(ctx, "⚠️ Error");
    }

    return reply_markdown(ctx, buffer);
}
