/* tg-bot — reply.c — Unified response formatting for command handlers. MIT License © 2026 ironmist45 */

/*
 * Provides a clean API for command handlers to send responses.
 * Automatically handles:
 *   - Response type selection (Markdown vs plain text)
 *   - Buffer overflow protection
 *   - Consistent logging of responses
 *   - Pre-formatted OK and error messages
 *
 * All response functions use the command_ctx_t structure which contains
 * the response buffer, size limit, and response type pointer.
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
 * Handles NULL pointer validation, buffer overflow protection,
 * response type assignment, and consistent logging.
 *
 * On overflow the buffer is null-terminated at the last byte and
 * -1 is returned. The truncated text remains in the buffer and will
 * be sent to the user as-is (graceful degradation).
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
    if (!ctx || !ctx->response || ctx->resp_size == 0)
        return -1;

    if (!text)
        text = "";

    int written = snprintf(ctx->response, ctx->resp_size, "%s", text);

    if (written < 0 || (size_t)written >= ctx->resp_size) {
        ctx->response[ctx->resp_size - 1] = '\0';
        LOG_CTX(LOG_CMD, ctx, LOG_WARN,
            "response truncated (text too long, limit=%zu)", ctx->resp_size);
        return -1;
    }

    if (ctx->resp_type)
        *(ctx->resp_type) = type;

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
 *
 * @param ctx   Command context
 * @param text  Error message (NULL for generic "Error")
 * @return      0 on success, -1 on error
 */
int reply_error(command_ctx_t *ctx, const char *text)
{
    char buffer[512];

    if (!text)
        text = "Error";

    int written = snprintf(buffer, sizeof(buffer), "⚠️ %s", text);

    if (written < 0 || (size_t)written >= sizeof(buffer))
        return reply_markdown(ctx, "⚠️ Error");

    return reply_markdown(ctx, buffer);
}
