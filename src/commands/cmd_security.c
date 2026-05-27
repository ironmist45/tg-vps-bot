/**
 * tg-bot - Telegram bot for system administration
 * cmd_security.c - /fail2ban and /sshkeys command handlers (V2)
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "cmd_security.h"
#include "logger.h"
#include "exec.h"
#include "utils.h"
#include "security.h"
#include "reply.h"
#include "config.h"
#include "metrics.h"

#include <stdio.h>
#include <string.h>

// ============================================================================
// /fail2ban COMMAND (V2)
// ============================================================================
/**
 * Manage Fail2Ban via f2b-wrapper
 *
 * Subcommands:
 *   /fail2ban status [jail]  - Show ban status (optionally for specific jail)
 *   /fail2ban ban <ip>       - Ban IP address in sshd jail
 *   /fail2ban unban <ip>     - Unban IP address from sshd jail
 *
 * Output is displayed in plain text (code block for status).
 * IP addresses are validated before passing to wrapper.
 *
 * @param ctx  Command context
 * @return     0 on success, -1 on error
 */

int cmd_fail2ban_v2(command_ctx_t *ctx)
{
    // No arguments: show usage
    if (!ctx->args || ctx->args[0] == '\0') {
        return reply_markdown(ctx,
            "🛡 *Fail2Ban*\n\n"
            "`/fail2ban status`\n"
            "`/fail2ban status sshd`\n"
            "`/fail2ban ban <ip>`\n"
            "`/fail2ban unban <ip>`");
    }

    // Parse subcommand and arguments
    char subcmd[32] = {0};
    char arg[64] = {0};

    LOG_CMD_CTX(ctx, LOG_DEBUG, "fail2ban: ctx->args='%s'", ctx->args ? ctx->args : "NULL");

    int parsed = sscanf(ctx->args, "%31s %63s", subcmd, arg);

    if (parsed < 1) {
        return reply_error(ctx, "Invalid command");
    }

    // ------------------------------------------------------------------------
    // STATUS subcommand
    // ------------------------------------------------------------------------
    if (strcmp(subcmd, "status") == 0) {
        char tmp[RESP_MAX];
        exec_result_t res;
        int rc;

        // Dynamically build args based on whether jail is specified
        if (arg[0] != '\0') {
            char *const args[] = {
                (char *)g_cfg.sudo_path, "-n",
                (char *)g_cfg.f2b_wrapper_path,
                "status",
                arg,
                NULL
            };
            rc = exec_command(args, tmp, sizeof(tmp), NULL, &res);
        } else {
            char *const args[] = {
                (char *)g_cfg.sudo_path, "-n",
                (char *)g_cfg.f2b_wrapper_path,
                "status",
                NULL
            };
            rc = exec_command(args, tmp, sizeof(tmp), NULL, &res);
        }

        if (rc != 0) {
            if (res.status == EXEC_TIMEOUT) {
                return reply_error(ctx, "Fail2Ban timeout");
            } else if (res.status == EXEC_EXEC_FAILED) {
                return reply_error(ctx, "Fail2Ban binary error\nPossible GLIBC mismatch");
            } else {
                return reply_error(ctx, "Fail2Ban failed");
            }
        }

        LOG_CMD_CTX(ctx, LOG_INFO, "fail2ban status: %s", arg[0] ? arg : "all");
        METRICS_CMD(fail2ban);
        safe_code_block(tmp, ctx->response, ctx->resp_size);

        if (ctx->resp_type) {
            *(ctx->resp_type) = RESP_PLAIN;
        }
        return 0;
    }

    // ------------------------------------------------------------------------
    // BAN subcommand
    // ------------------------------------------------------------------------
    if (strcmp(subcmd, "ban") == 0) {
        if (arg[0] == '\0') {
            return reply_error(ctx, "Usage: /fail2ban ban <ip>");
        }

        if (!is_safe_ip(arg)) {
            LOG_CMD_CTX(ctx, LOG_WARN, "fail2ban ban: invalid IP %s", arg);
            return reply_error(ctx, "Invalid IP address");
        }

        char *const args[] = {
            (char *)g_cfg.sudo_path, "-n",
            (char *)g_cfg.f2b_wrapper_path,
            "set",
            "sshd",
            "banip",
            arg,
            NULL
        };

        char tmp[RESP_MAX];
        exec_result_t res;

        if (exec_command(args, tmp, sizeof(tmp), NULL, &res) != 0) {
            return reply_error(ctx, "Ban failed");
        }

        if (res.status == EXEC_EXIT_NONZERO) {
            safe_code_block(tmp, ctx->response, ctx->resp_size);
            if (ctx->resp_type) *(ctx->resp_type) = RESP_PLAIN;
            return -1;
        }

        LOG_CMD_CTX(ctx, LOG_INFO, "fail2ban ban: %s", arg);
        METRICS_CMD(fail2ban);
        return reply_ok(ctx, "IP banned");
    }

    // ------------------------------------------------------------------------
    // UNBAN subcommand
    // ------------------------------------------------------------------------
    if (strcmp(subcmd, "unban") == 0) {
        if (arg[0] == '\0') {
            return reply_error(ctx, "Usage: /fail2ban unban <ip>");
        }

        if (!is_safe_ip(arg)) {
            LOG_CMD_CTX(ctx, LOG_WARN, "fail2ban unban: invalid IP %s", arg);
            return reply_error(ctx, "Invalid IP address");
        }

        char *const args[] = {
            (char *)g_cfg.sudo_path, "-n",
            (char *)g_cfg.f2b_wrapper_path,
            "set",
            "sshd",
            "unbanip",
            arg,
            NULL
        };

        char tmp[RESP_MAX];
        exec_result_t res;

        if (exec_command(args, tmp, sizeof(tmp), NULL, &res) != 0) {
            return reply_error(ctx, "Unban failed");
        }

        if (res.status == EXEC_EXIT_NONZERO) {
            safe_code_block(tmp, ctx->response, ctx->resp_size);
            if (ctx->resp_type) *(ctx->resp_type) = RESP_PLAIN;
            return -1;
        }

        LOG_CMD_CTX(ctx, LOG_INFO, "fail2ban unban: %s", arg);
        METRICS_CMD(fail2ban);
        return reply_ok(ctx, "IP unbanned");
    }

    // ------------------------------------------------------------------------
    // Unknown subcommand
    // ------------------------------------------------------------------------
    LOG_CMD_CTX(ctx, LOG_WARN, "fail2ban unknown subcommand: %s", subcmd);
    return reply_error(ctx, "Unknown fail2ban command");
}

// ============================================================================
// /sshkeys COMMAND (V2)
// ============================================================================

/**
 * Show SSH authorized keys from configured path (V2 handler)
 *
 * Reads SSH_KEYS_PATH via 'sudo cat' and displays key type and comment
 * for each entry — the key itself is not shown (it is long, binary-encoded
 * and meaningless to a human reader).
 *
 * If SSH_KEYS_PATH is not set in config, returns a plain-text hint
 * explaining how to enable the feature. The command is always registered
 * in the command table regardless of config state.
 *
 * authorized_keys line format (OpenSSH):
 *   [options] <keytype> <base64key> [comment]
 *
 * Correctly handles an optional leading options field, including quoted
 * values with embedded spaces (e.g. from="1.2.3.4", command="...").
 *
 * Requires sudoers entry:
 *   tg-bot ALL=(ALL) NOPASSWD: /bin/cat /home/user/.ssh/authorized_keys
 *
 * @param ctx  Command context
 * @return     0 on success, -1 on error
 */

/* Known OpenSSH public key type identifiers */
static const char *s_key_types[] = {
    "ssh-rsa",
    "ssh-dss",
    "ssh-ed25519",
    "ecdsa-sha2-nistp256",
    "ecdsa-sha2-nistp384",
    "ecdsa-sha2-nistp521",
    "sk-ssh-ed25519@openssh.com",
    "sk-ecdsa-sha2-nistp256@openssh.com",
    NULL
};

/*
 * Returns a short display name for a key type:
 *   "ssh-ed25519"         -> "ed25519"
 *   "ecdsa-sha2-nistp256" -> "nistp256"
 *   "sk-ssh-ed25519@..."  -> "ed25519@openssh.com"
 */
static const char *sshkeys_pretty_type(const char *raw)
{
    if (strncmp(raw, "ssh-",        4)  == 0) return raw + 4;
    if (strncmp(raw, "ecdsa-sha2-", 11) == 0) return raw + 11;
    if (strncmp(raw, "sk-ssh-",     7)  == 0) return raw + 7;
    if (strncmp(raw, "sk-ecdsa-",   9)  == 0) return raw + 9;
    return raw;
}

/* Returns 1 if token matches a known OpenSSH key type, 0 otherwise */
static int sshkeys_is_keytype(const char *token)
{
    for (int i = 0; s_key_types[i]; i++)
        if (strcmp(token, s_key_types[i]) == 0)
            return 1;
    return 0;
}

/*
 * Parse one line from authorized_keys.
 *
 * Fills out_type (short display name) and out_comment.
 * Returns 1 on success, 0 if line should be skipped
 * (empty, comment '#', unknown format).
 *
 * Correctly skips an optional leading options field, including
 * quoted values with embedded spaces and backslash escapes.
 */
static int sshkeys_parse_line(const char *line,
                              char *out_type,    size_t type_sz,
                              char *out_comment, size_t comment_sz)
{
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0' || *p == '#') return 0;

    char buf[4096];
    strncpy(buf, p, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    buf[strcspn(buf, "\r\n")] = '\0';

    char *pos = buf;
    char *keytype_end = NULL;

    while (*pos) {
        while (*pos == ' ' || *pos == '\t') pos++;
        if (*pos == '\0') break;

        /* Skip quoted option value (e.g. command="...", from="...") */
        if (*pos == '"') {
            pos++;
            while (*pos && *pos != '"') {
                if (*pos == '\\' && *(pos + 1)) pos++;
                pos++;
            }
            if (*pos == '"') pos++;
            continue;
        }

        /* Read whitespace-delimited token */
        char *tok_start = pos;
        while (*pos && *pos != ' ' && *pos != '\t') pos++;

        size_t tok_len = (size_t)(pos - tok_start);
        if (tok_len == 0) continue;

        char token[128];
        if (tok_len >= sizeof(token)) tok_len = sizeof(token) - 1;
        memcpy(token, tok_start, tok_len);
        token[tok_len] = '\0';

        if (sshkeys_is_keytype(token)) {
            const char *pretty = sshkeys_pretty_type(token);
            size_t copy_len = strlen(pretty);
            if (copy_len >= type_sz) copy_len = type_sz - 1;
            memcpy(out_type, pretty, copy_len);
            out_type[copy_len] = '\0';
            keytype_end = pos;
            break;
        }
        /* Not a keytype — options token, keep walking */
    }

    if (!keytype_end) return 0;

    /* Skip base64 key blob (next whitespace-delimited token) */
    char *p2 = keytype_end;
    while (*p2 == ' ' || *p2 == '\t') p2++;
    while (*p2 && *p2 != ' ' && *p2 != '\t') p2++;

    /* Remainder is the comment */
    while (*p2 == ' ' || *p2 == '\t') p2++;
    snprintf(out_comment, comment_sz, "%s", (*p2 != '\0') ? p2 : "(no comment)");
    return 1;
}

int cmd_sshkeys_v2(command_ctx_t *ctx)
{
    /* Feature disabled — explain how to enable */
    if (g_cfg.ssh_keys_path[0] == '\0') {
        return reply_plain(ctx,
            "ℹ️ SSH keys not configured.\n\n"
            "Add to config:\n"
            "SSH_KEYS_PATH=/home/user/.ssh/authorized_keys\n\n"
            "Then reload: kill -HUP $(pidof tg-bot)");
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "sshkeys: reading %s", g_cfg.ssh_keys_path);

    /* Read via sudo cat — bot runs as tg-bot, not as file owner */
    char *const args[] = {
        (char *)g_cfg.sudo_path, "-n",
        "/bin/cat",
        (char *)g_cfg.ssh_keys_path,
        NULL
    };

    char tmp[RESP_MAX];
    exec_result_t res;

    int rc = exec_command(args, tmp, sizeof(tmp), NULL, &res);

    if (rc != 0) {
        LOG_CMD_CTX(ctx, LOG_WARN, "sshkeys: exec failed status=%d", res.status);
        if (res.status == EXEC_TIMEOUT)
            return reply_error(ctx, "Timeout reading authorized_keys");
        if (res.status == EXEC_EXEC_FAILED)
            return reply_error(ctx, "Failed to execute sudo cat");
        return reply_error(ctx,
            "Cannot read authorized_keys\n"
            "Check sudo rules for tg-bot");
    }

    /* Parse lines */
    typedef struct { char type[32]; char comment[128]; } key_entry_t;
    key_entry_t keys[64];
    int key_count = 0;

    char *line = tmp;
    while (line && *line && key_count < 64) {
        char *next = strchr(line, '\n');
        if (next) *next = '\0';

        char type[32]     = {0};
        char comment[128] = {0};

        if (sshkeys_parse_line(line, type, sizeof(type),
                               comment, sizeof(comment))) {
            snprintf(keys[key_count].type,    sizeof(keys[0].type),    "%s", type);
            snprintf(keys[key_count].comment, sizeof(keys[0].comment), "%s", comment);
            key_count++;
        }

        line = next ? next + 1 : NULL;
    }

    /* Build reply — plain text, comments may contain arbitrary characters */
    char reply[1024];
    int  off = 0;

    off += snprintf(reply + off, sizeof(reply) - off, "🔑 SSH Keys\n\n");

    if (key_count == 0) {
        off += snprintf(reply + off, sizeof(reply) - off,
                        "No keys found in:\n%s", g_cfg.ssh_keys_path);
    } else {
        for (int i = 0; i < key_count && off < (int)sizeof(reply) - 64; i++) {
            off += snprintf(reply + off, sizeof(reply) - off,
                            "%d. %s — %s\n",
                            i + 1, keys[i].type, keys[i].comment);
        }
        off += snprintf(reply + off, sizeof(reply) - off,
                        "\nTotal: %d key%s",
                        key_count, key_count == 1 ? "" : "s");
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "sshkeys: found %d %s", key_count, key_count == 1 ? "key" : "keys");
    METRICS_CMD(sshkeys);

    return reply_plain(ctx, reply);
}
