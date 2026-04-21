/**
 * tg-bot - Telegram bot for system administration
 * 
 * security.c - Access control and token-based confirmation
 * 
 * Provides security mechanisms for the bot:
 *   - Single-user access control (chat_id validation)
 *   - Input sanitization (control character filtering)
 *   - Rate limiting (prevent command spam)
 *   - Stateless reboot tokens (confirmation for critical actions)
 *   - Bruteforce protection (temporary block after failed attempts)
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

#include "security.h"
#include "logger.h"
#include "telegram.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

// ⚠️ SECURITY NOTE:
// This is NOT a cryptographic secret.
// Tokens are protected by runtime salt (generated at startup).
// Purpose:
//   - prevent accidental execution
//   - add confirmation step for critical actions
// Real security is enforced by chat_id validation.
#define TOKEN_SALT 0x5F3759DF

// ============================================================================
// INTERNAL STATE
// ============================================================================

// Access control
static long g_allowed_chat_id = 0;
static int g_token_ttl = 60;

// Runtime token salt (generated at startup, not persistent)
static unsigned int g_runtime_salt = 0;

// Last generated token (for replay protection)
static int g_last_token = -1;
static time_t g_last_token_time = 0;

// Bruteforce protection
static int g_failed_attempts = 0;
static time_t g_block_until = 0;

// Rate limiting
static time_t g_last_cmd_time = 0;
static int g_cmd_burst = 0;

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * Initialize security module
 * 
 * Generates runtime salt for token generation. Must be called once
 * at program startup.
 */
void security_init(void) {
    unsigned int seed = (unsigned int)(
        time(NULL) ^ getpid() ^ clock()
    );

    srand(seed);
    g_runtime_salt = (unsigned int)rand();

    LOG_SEC(LOG_INFO, "Security initialized (stateless tokens)");
    LOG_SEC(LOG_DEBUG, "runtime salt=0x%X", g_runtime_salt);
}

// ============================================================================
// CONFIGURATION
// ============================================================================

/**
 * Set the allowed Telegram chat ID (single-user mode)
 * 
 * @param chat_id  Telegram chat ID of the authorized user
 */
void security_set_allowed_chat(long chat_id) {
    g_allowed_chat_id = chat_id;
}

/**
 * Set token time-to-live for reboot confirmation
 * 
 * @param ttl  Token validity period in seconds (1-3600)
 */
void security_set_token_ttl(int ttl) {
    if (ttl > 0 && ttl <= 3600)
        g_token_ttl = ttl;
}

// ============================================================================
// ACCESS CONTROL
// ============================================================================

/**
 * Check if chat ID matches the allowed user
 * 
 * @param chat_id  Telegram chat ID to validate
 * @return         1 if allowed, 0 otherwise
 */
int security_is_allowed_chat(long chat_id) {

    // Protect against uninitialized state
    if (g_allowed_chat_id == 0) {
        LOG_SEC(LOG_ERROR, "allowed_chat_id not initialized");
        return 0;
    }

    return chat_id == g_allowed_chat_id;
}

/**
 * Validate text input for safety (no binary/control characters)
 * 
 * Allows only printable characters, newlines, carriage returns, and tabs.
 * Rejects strings containing binary data or other control characters.
 * 
 * @param text  Input string to validate
 * @return      0 if valid, -1 if invalid or too long
 */
int security_validate_text(const char *text) {
    
    if (!text)
        return -1;

    size_t len = strlen(text);

    // Reject empty or excessively long input
    if (len == 0 || len > 1024)
        return -1;

    // Scan for prohibited control characters
    for (size_t i = 0; i < len; i++) {
        
        unsigned char c = text[i];

        // Reject control characters except newline, carriage return, tab
        if (c < 32 &&
            c != '\n' &&
            c != '\r' &&
            c != '\t') {
            return -1;
        }
    }

    return 0;
}

/**
 * Check access for a specific command
 * 
 * Verifies that the requesting chat ID is authorized and logs the result.
 * 
 * @param chat_id  Telegram chat ID making the request
 * @param cmd      Command being executed (for logging)
 * @param req_id   16-bit request identifier (for log correlation)
 * @return         0 if access granted, -1 if denied
 */
int security_check_access(long chat_id, const char *cmd, unsigned short req_id) {

    int allowed = security_is_allowed_chat(chat_id);

    LOG_SEC(LOG_INFO,
            "poll=%04x req=%04x ACCESS CHECK: chat_id=%ld cmd=%s result=%s",
            telegram_get_poll_id(),
            req_id,
            chat_id,
            cmd,
            allowed ? "ALLOW" : "DENY");

    if (!allowed) {
        LOG_STATE(LOG_WARN,
            "ACCESS DENIED: req=%04x cmd=%s chat_id=%ld",
             req_id,
             cmd ? cmd : "NULL",
             chat_id);

        return -1;
    }

    return 0;
}

// ============================================================================
// BRUTEFORCE PROTECTION
// ============================================================================

/**
 * Register failed token validation attempt
 * 
 * Implements exponential backoff: after 5 consecutive failures,
 * blocks further attempts for 30 seconds.
 * 
 * @param now  Current timestamp
 */
static void register_failed_attempt(time_t now) {

    g_failed_attempts++;

    LOG_SEC(LOG_DEBUG,
        "failed attempts: %d",
        g_failed_attempts);

    if (g_failed_attempts >= 5) {
        g_block_until = now + 30;
        g_failed_attempts = 0;

        LOG_SEC(LOG_WARN,
            "Too many invalid tokens, blocking for 30 seconds");
    }
}

// ============================================================================
// RATE LIMITING
// ============================================================================

/**
 * Apply rate limiting to prevent command spam
 * 
 * Allows up to 5 commands per second before throttling.
 * 
 * @return  0 if request allowed, -1 if rate limit exceeded
 */
int security_rate_limit(void) {

    time_t now = time(NULL);

    if (now == g_last_cmd_time) {
        g_cmd_burst++;

        if (g_cmd_burst > 5) {
            LOG_SEC(LOG_WARN, "Rate limit triggered");
            return -1;
        }
    } else {
        g_last_cmd_time = now;
        g_cmd_burst = 1;
    }

    return 0;
}

// ============================================================================
// TOKEN GENERATION (STATELESS)
// ============================================================================

/**
 * Generate hash for token calculation
 * 
 * Combines chat_id, timestamp, compile-time salt, and runtime salt.
 * The result is mixed to improve distribution.
 * 
 * @param chat_id  Authorized chat ID
 * @param ts       Timestamp for this token window
 * @return         Integer hash in range [0, 999999]
 */
static int make_token(long chat_id, time_t ts) {

    unsigned int x = (unsigned int)(chat_id ^ ts ^ TOKEN_SALT ^ g_runtime_salt);

    // Mix bits for better distribution
    x ^= (x >> 16);
    x *= 0x45d9f3b;
    x ^= (x >> 16);

    return (int)(x % 1000000);
}

// ============================================================================
// PUBLIC TOKEN API
// ============================================================================

/**
 * Generate a new reboot confirmation token
 * 
 * Token is stateless: derived from chat_id, current time, and salts.
 * Stored internally for replay protection.
 * 
 * @param chat_id  Authorized chat ID requesting the token
 * @return         6-digit token (000000-999999)
 */
int security_generate_reboot_token(long chat_id) {

    time_t ts = time(NULL);

    int token = make_token(chat_id, ts);
    int final_token = (ts % 1000000) ^ token;

    LOG_SEC(LOG_INFO,
        "Generated token: chat_id=%ld ts=%d token=%06d",
        chat_id, ts, final_token);
    LOG_SEC(LOG_DEBUG,
        "token components: ts_mod=%d raw=%d",
        ts % 1000000, token);

    // Store for replay protection
    g_last_token = final_token;
    g_last_token_time = ts;

    return final_token;
}

/**
 * Validate a reboot confirmation token
 * 
 * Checks token against time windows within TTL. Also enforces:
 *   - Token format (6 digits)
 *   - Bruteforce block status
 *   - Replay protection (single-use)
 * 
 * @param chat_id      Authorized chat ID
 * @param input_token  Token to validate (provided by user)
 * @return             0 if valid, -1 if invalid/expired/replayed
 */
int security_validate_reboot_token(long chat_id, int input_token) {

    // Validate token format (must be 6-digit positive integer)
    if (input_token < 0 || input_token > 999999) {
        LOG_SEC(LOG_WARN,
            "Invalid token format: %d (chat_id=%ld)",
            input_token, chat_id);
        return -1;
    }

    time_t now = time(NULL);

    // Check if currently blocked (bruteforce protection)
    if (now < g_block_until) {
        LOG_SEC(LOG_WARN, "Token validation blocked (bruteforce)");
        return -1;
    }

    // Check all time windows within TTL
    for (int i = 0; i <= g_token_ttl; i++) {

        time_t ts = (now - i);
        int expected = (ts % 1000000) ^ make_token(chat_id, ts);

        LOG_SEC(LOG_DEBUG,
            "check: ts=%d expected=%06d input=%06d",
            ts, expected, input_token);

        if (expected == input_token) {

            // Replay protection: must be the exact token we generated
            if (input_token == g_last_token &&
                (now - g_last_token_time) <= g_token_ttl) {

                // Invalidate after use (single-use token)
                g_last_token = -1;

                LOG_SEC(LOG_INFO,
                    "Token accepted: chat_id=%ld token=%06d (age=%d sec)",
                    chat_id, input_token, i);

                // Reset bruteforce state on success
                g_failed_attempts = 0;
                g_block_until = 0;
                
                return 0;
            }

            LOG_SEC(LOG_WARN,
                "Replay or expired token: %06d",
                input_token);

            register_failed_attempt(now);
            return -1;
        }
    }

    LOG_SEC(LOG_WARN,
        "Invalid token: chat_id=%ld token=%06d",
        chat_id, input_token);

    register_failed_attempt(now);
    
    return -1;
}
