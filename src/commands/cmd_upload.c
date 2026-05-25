/**
 * tg-bot - Telegram bot for system administration
 * cmd_upload.c - File upload command handlers (/files, incoming files)
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "cmd_upload.h"
#include "upload.h"
#include "config.h"
#include "logger.h"
#include "reply.h"
#include "telegram.h"
#include "metrics.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// ============================================================================
// INCOMING FILE HANDLER
// ============================================================================

/**
 * Handle an incoming file sent to the bot.
 *
 * ctx->args is overloaded here to carry file_id and file_name — these
 * are set by telegram_poll.c before calling this handler. The standard
 * command flow does not apply (no slash command, no args string).
 *
 * The function uses two separate telegram_send() calls:
 *   1. Immediate "📥 Uploading..." acknowledgement so the user knows
 *      the bot received the file (download may take a few seconds)
 *   2. Final result "✅ Saved: <name> (<size>)" or error
 *
 * @param ctx  Command context with file_id/file_name set by poll handler
 * @return     0 on success, -1 on error
 */
int cmd_handle_upload(command_ctx_t *ctx)
{
    /*
     * ctx->args carries "file_id\nfile_name" — packed by telegram_poll.c.
     * Split on '\n' to extract the two fields.
     */
    if (!ctx->args || ctx->args[0] == '\0') {
        LOG_CMD_CTX(ctx, LOG_ERROR, "upload: missing file_id in ctx->args");
        return reply_plain(ctx, "❌ Internal error: missing file_id");
    }

    /* Copy to avoid modifying ctx->args */
    char args_copy[UPLOAD_FILE_ID_MAX + UPLOAD_FILENAME_MAX + 2];
    snprintf(args_copy, sizeof(args_copy), "%s", ctx->args);

    char *file_id   = args_copy;
    char *file_name = NULL;

    char *sep = strchr(args_copy, '\n');
    if (sep) {
        *sep      = '\0';
        file_name = sep + 1;
        if (file_name[0] == '\0')
            file_name = NULL;
    }

    LOG_CMD_CTX(ctx, LOG_INFO,
        "upload: incoming file file_id=%.32s... name=%s",
        file_id, file_name ? file_name : "(none)");

    /* Step 1 — send immediate acknowledgement */
    telegram_send(ctx->chat_id, "📥 Uploading\\.\\.\\.", RESP_MARKDOWN);

    /* Step 2 — download and save the file */
    char saved_path[UPLOAD_PATH_MAX] = {0};

    if (upload_receive_file(file_id, file_name,
                            ctx->req_id,
                            saved_path, sizeof(saved_path)) != 0) {
        LOG_CMD_CTX(ctx, LOG_ERROR, "upload: upload_receive_file failed");
        METRICS_CMD(other);
        return reply_plain(ctx, "❌ Upload failed");
    }

    /* Step 3 — get saved file size for the success message */
    struct stat st;
    long saved_bytes = 0;
    if (stat(saved_path, &st) == 0)
        saved_bytes = (long)st.st_size;

    /* Format human-readable size */
    char size_str[32];
    if (saved_bytes < 1024)
        snprintf(size_str, sizeof(size_str), "%ld B", saved_bytes);
    else if (saved_bytes < 1024 * 1024)
        snprintf(size_str, sizeof(size_str), "%.1f KB",
                 (double)saved_bytes / 1024.0);
    else
        snprintf(size_str, sizeof(size_str), "%.1f MB",
                 (double)saved_bytes / (1024.0 * 1024.0));

    /* Extract filename from saved_path for the success message */
    const char *slash    = strrchr(saved_path, '/');
    const char *disp_name = slash ? slash + 1 : saved_path;

    LOG_CMD_CTX(ctx, LOG_INFO,
        "upload: saved '%s' (%s)", saved_path, size_str);

    /* Step 4 — send success reply */
    snprintf(ctx->response, ctx->resp_size,
             "✅ Saved: `%s` \\(%s\\)", disp_name, size_str);
    *ctx->resp_type = RESP_MARKDOWN;

    METRICS_CMD(other);
    return 0;
}

// ============================================================================
// /files COMMAND (V2)
// ============================================================================

/**
 * List files in the upload directory.
 *
 * Output format:
 *   📁 *UPLOADS*
 *
 *   `config.conf` — 1.2 KB
 *   `script.sh` — 4.5 KB
 *
 *   Total: 2 files
 *
 * @param ctx  Command context
 * @return     0 on success, -1 on error
 */
int cmd_files_v2(command_ctx_t *ctx)
{
    char buf[2048];

    if (upload_list_files(buf, sizeof(buf), ctx->req_id) != 0) {
        return reply_error(ctx, "Failed to list files");
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "files: requested");
    METRICS_CMD(other);

    return reply_markdown(ctx, buf);
}
