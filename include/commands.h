/**
 * tg-bot - Telegram bot for system administration
 * commands.h - Command dispatcher types and API
 * MIT License - Copyright (c) 2026 ironmist45
 */

#ifndef COMMANDS_H
#define COMMANDS_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

// ============================================================================
// RESPONSE TYPES
// ============================================================================

/**
 * Telegram message formatting mode
 */
typedef enum {
    RESP_MARKDOWN = 0,  /* Parse as MarkdownV2 (escaped by telegram.c) */
    RESP_PLAIN    = 1   /* Plain text, no formatting                    */
} response_type_t;

// ============================================================================
// V2 HANDLER CONTEXT (preferred)
// ============================================================================

/**
 * Command execution context for V2 handlers
 *
 * Provides all information needed to process a command and send a response.
 * Passed by pointer to command_handler_v2_t functions.
 */
typedef struct {
    /* Request metadata */
    long          chat_id;      /* Telegram chat ID of requester            */
    int           user_id;      /* Telegram user ID                         */
    const char   *username;     /* Telegram username (may be NULL)          */
    const char   *args;         /* Arguments after command name (may be NULL) */
    const char   *raw_text;     /* Full raw message text                    */
    time_t        msg_date;     /* Message timestamp (for /ping latency)    */

    /* Response output */
    char          *response;    /* Response buffer (pre-allocated)          */
    size_t         resp_size;   /* Size of response buffer                  */
    response_type_t *resp_type; /* Output: response type to use             */

    /* Logging */
    unsigned short req_id;      /* 16-bit request ID for log correlation    */
} command_ctx_t;

// ============================================================================
// V2 HANDLER (context style)
// ============================================================================

/**
 * V2 command handler signature (preferred)
 *
 * Uses command_ctx_t for cleaner API and easier extension.
 * Recommended for all new command implementations.
 *
 * @param ctx  Command context (contains all request data and response buffer)
 * @return     0 on success, -1 on error
 */
typedef int (*command_handler_v2_t)(command_ctx_t *ctx);

// ============================================================================
// COMMAND TABLE ENTRY
// ============================================================================

/**
 * Command registration entry
 *
 * Populated statically in commands.c and used by commands_handle().
 *
 * requires_confirmation:
 *   When set to 1 the dispatcher intercepts the command before calling
 *   the handler and requests a confirmation code from the user:
 *
 *     - If g_cfg.totp_secret is set: TOTP flow (Google Authenticator / Aegis)
 *     - If g_cfg.totp_secret is empty: classic stateless token flow (fallback)
 *
 *   The user must send /confirm <code> to proceed.
 *   Commands with requires_confirmation = 1 are marked 🔐 in /help.
 *
 *   Commands that need conditional confirmation (only for some actions,
 *   e.g. /service) use requires_confirmation = 0 and call
 *   commands_request_confirm() directly from their handler.
 */
typedef struct {
    const char            *name;        /* Command string e.g. "/status"    */
    command_handler_v2_t   handler_v2;  /* V2 handler                       */
    const char            *description; /* For /help (NULL = hidden)        */
    const char            *category;    /* For /help grouping (NULL = hidden)*/
    int requires_confirmation;          /* 1 = needs /confirm before exec   */
} command_t;

// ============================================================================
// PENDING CONFIRMATION STATE
// ============================================================================

/**
 * State for a command awaiting confirmation.
 *
 * Single slot — the bot is single-user so at most one operation
 * can be pending at any given time.
 *
 * Populated by the dispatcher when requires_confirmation = 1 and
 * consumed (cleared) when /confirm <code> is received.
 *
 * args: original command arguments saved at request time so the handler
 * can retrieve them after confirmation. For example "/service ssh restart"
 * stores "ssh restart" here so cmd_service_v2() knows which service and
 * action to execute when /confirm arrives.
 */
typedef struct {
    long   chat_id;         /* Who initiated the command                    */
    char   command[32];     /* Command name e.g. "/reboot"                  */
    char   args[64];        /* Original arguments e.g. "ssh restart"        */
    int    token;           /* Expected token (classic flow only)           */
    time_t expires_at;      /* When this pending entry expires              */
    int    active;          /* 1 = waiting for confirmation, 0 = empty      */
} pending_confirm_t;

// ============================================================================
// COMMAND DISPATCHER API
// ============================================================================

/**
 * Route incoming message to appropriate command handler.
 *
 * Parses command name, performs security checks, and dispatches to the
 * registered handler. For commands with requires_confirmation = 1:
 *   - First call  → stores pending state, asks user for /confirm <code>
 *   - /confirm    → validates code (TOTP or token), executes original command
 *
 * Security checks performed:
 *   - Input validation (no binary/control characters)
 *   - Rate limiting (max 5 commands/second)
 *   - Access control (single-user mode)
 *
 * @param text       Full message text (must start with '/')
 * @param chat_id    Telegram chat ID
 * @param msg_date   Message timestamp (from Telegram)
 * @param user_id    Telegram user ID
 * @param username   Telegram username (may be NULL)
 * @param req_id     16-bit request ID for log correlation
 * @param response   Output buffer for response
 * @param resp_size  Size of response buffer
 * @param resp_type  Output: response type used
 * @return           0 on success, -1 on error or unknown command
 */
int commands_handle(const char *text,
                    long chat_id,
                    time_t msg_date,
                    int user_id,
                    const char *username,
                    unsigned short req_id,
                    char *response,
                    size_t resp_size,
                    response_type_t *resp_type);

/**
 * Request confirmation for a command from inside a handler.
 *
 * Used by handlers registered with requires_confirmation = 0 that need
 * conditional confirmation (e.g. /service — only start/stop/restart need it).
 *
 * Populates g_pending, generates a TOTP prompt or classic token, and writes
 * the confirmation message into ctx->response. The handler should return 0
 * immediately after calling this function.
 *
 * When /confirm arrives the dispatcher restores g_pending.args into ctx->args
 * and calls the handler again — on the second call the handler executes the
 * actual operation instead of requesting confirmation.
 *
 * @param ctx       Command context (chat_id, req_id, response buffer)
 * @param cmd_name  Command name to store in pending slot (e.g. "/service")
 * @param args      Arguments to save in pending slot (e.g. "ssh restart")
 * @return          0 always (confirmation message written to ctx->response)
 */
int commands_request_confirm(command_ctx_t *ctx,
                             const char *cmd_name,
                             const char *args);

#endif /* COMMANDS_H */
