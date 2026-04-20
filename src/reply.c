#include "reply.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>

static int reply_write(command_ctx_t *ctx,
                       response_type_t type,
                       const char *text)
{
    if (!ctx || !ctx->response || ctx->resp_size == 0) {
        return -1;
    }

    if (!text) {
        text = "";
    }

    int written = snprintf(ctx->response,
                           ctx->resp_size,
                           "%s",
                           text);

    if (written < 0 || (size_t)written >= ctx->resp_size) {
        ctx->response[ctx->resp_size - 1] = '\0';
        return -1;
    }

    if (ctx->resp_type) {
        *(ctx->resp_type) = type;
    }

    // 🔥 LOG RESPONSE (v2 unified)
    LOG_CTX(LOG_CMD, ctx, LOG_DEBUG,
        "RES %s (%zu chars)",
        ctx->raw_text ? ctx->raw_text : "NULL",
        strlen(ctx->response));
  
    return 0;
}

int reply_plain(command_ctx_t *ctx, const char *text)
{
    return reply_write(ctx, RESP_PLAIN, text);
}

int reply_markdown(command_ctx_t *ctx, const char *text)
{
    return reply_write(ctx, RESP_MARKDOWN, text);
}

int reply_ok(command_ctx_t *ctx, const char *text)
{
    return reply_markdown(ctx, text ? text : "OK");
}

int reply_error(command_ctx_t *ctx, const char *text)
{
    char buffer[256];

    if (!text) {
        text = "Error";
    }

    int written = snprintf(buffer, sizeof(buffer),
                           "⚠️ %s", text);

    if (written < 0 || (size_t)written >= sizeof(buffer)) {
        return reply_markdown(ctx, "⚠️ Error");
    }

    return reply_markdown(ctx, buffer);
}
