/**
 * tg-bot - Telegram bot for system administration
 * commands.h - Command dispatcher types and API
 * MIT License - Copyright (c) 2026
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
    RESP_MARKDOWN = 0,  // Parse as MarkdownV2 (escaped by telegram.c)
    RESP_PLAIN    = 1   // Plain text, no formatting
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
    // Request metadata
    long chat_id;              // Telegram chat ID of requester
    int user_id;               // Telegram user ID
    const char *username;      // Telegram username (may be NULL)
    const char *args;          // Arguments after command name (may be NULL)
    const char *raw_text;      // Full raw message text
    time_t msg_date;           // Message timestamp (for /ping latency)

    // Response output
    char *response;            // Response buffer (pre-allocated)
    size_t resp_size;          // Size of response buffer
    response_type_t *resp_type; // Output: response type to use

    // Logging
    unsigned short req_id;     // 16-bit request ID for log correlation
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
 * Supports V2 handlers only.
 */
typedef struct {
    const char *name;                    // Command name including slash (e.g., "/status")
    command_handler_v2_t handler_v2;     // V2 handler (preferred, may be NULL)
    const char *description;             // Description for /help (NULL = hidden)
    const char *category;                // Category for /help grouping (NULL = hidden)
} command_t;

// ============================================================================
// COMMAND DISPATCHER API
// ============================================================================

/**
 * Route incoming message to appropriate command handler
 * 
 * Parses command name, performs security checks, and dispatches to the
 * registered handler (V2 preferred, falls back to legacy).
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

#endif // COMMANDS_H
