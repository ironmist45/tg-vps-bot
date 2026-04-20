// ==== Core reply API for tg-bot ====
// reply_plain:
// writes plain text response into ctx->response
// sets response type to RESP_PLAIN
// reply_markdown:
// writes markdown-formatted response
// sets response type to RESP_MARKDOWN
// reply_error:
// helper for error responses (adds ⚠️ prefix)
// ==================================

#ifndef REPLY_H
#define REPLY_H

#include <stddef.h>
#include "commands.h"

// ===== Core reply API =====

// plain text response
int reply_plain(command_ctx_t *ctx, const char *text);

// markdown response
int reply_markdown(command_ctx_t *ctx, const char *text);

// ===== Convenience helpers =====

// success response (markdown by default)
int reply_ok(command_ctx_t *ctx, const char *text);

// error response (markdown with ⚠️ prefix)
int reply_error(command_ctx_t *ctx, const char *text);

#endif
