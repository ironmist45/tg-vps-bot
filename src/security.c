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
 * Copyright (c) 2026 ironmist45
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
#include <sys/random.h>

// ⚠️ SECURITY NOTE:
// This is NOT a cryptographic secret.
// Tokens are protected by runtime salt (generated at startup via getrandom).
// Purpose:
//   - prevent accidental execution
//   - add confirmation step for critical actions
// Real security is enforced by chat_id validation.
#define TOKEN_SALT 0x5F3759DF

/*
 * Maximum burst count for rate limiting.
 * Capped at this value to prevent integer overflow:
 * once the limit is exceeded g_cmd_burst stays pinned at
 * RATE_LIMIT_MAX rather than growing without bound.
 */
#define RATE_LIMIT_MAX 16

// ============================================================================
// INTERNAL STATE
// ============================================================================

// Access control
static long g_allowed_chat_id = 0;
static int  g_token_ttl       = 60;

// Runtime token salt (generated at startup via getrandom, not persistent)
static unsigned int g_runtime_salt = 0;

// Last generated token (for replay protection)
static int    g_last_token      = -1;
static time_t g_last_token_time = 0;

// Bruteforce protection
static int    g_failed_attempts = 0;
static time_t g_block_until     = 0;

// Rate limiting
static time_t g_last_cmd_time = 0;
static int    g_cmd_burst     = 0;

// ============================================================================
// INITIALIZATION
// ============================================================================

/**
 * Initialize security module.
 *
 * Generates runtime salt via getrandom(2) — a non-blocking kernel CSPRNG
 * available since Linux 3.17 (Ubuntu 18.04 ships kernel 4.15+).
 * Falls back to /dev/urandom if the syscall unexpectedly fails.
 *
 * Must be called once at program startup before any other security functions.
 */
void security_init(void) {

    unsigned int salt = 0;

    /*
     * getrandom(2) with flags=0 blocks until the kernel entropy pool is
     * initialised (only relevant at very early boot) and cannot be
     * interrupted by signals for requests <= 256 bytes.  This is the
     * correct way to obtain unpredictable bytes on Linux.
     */
    ssize_t n = getrandom(&salt, sizeof(salt), 0);

    if (n != (ssize_t)sizeof(salt)) {
        /*
         * Extremely unlikely on a running system; log and fall back to
         * /dev/urandom rather than using a predictable seed.
         */
        LOG_SEC(LOG_WARN,
            "getrandom() failed (n=%zd), falling back to /dev/urandom", n);

        FILE *f = fopen("/dev/urandom", "rb");
        if (f) {
            if (fread(&salt, sizeof(salt), 1, f) != 1) {
                LOG_SEC(LOG_ERROR,
                    "fread /dev/urandom failed -- salt will be zero");
                salt = 0;
            }
            fclose(f);
        } else {
            LOG_SEC(LOG_ERROR,
                "cannot open /dev/urandom -- salt will be zero");
            salt = 0;
        }
    }

    g_runtime_salt = salt;

    LOG_SEC(LOG_INFO, "Security initialized (stateless tokens, getrandom salt)");
    /* Do NOT log the salt value -- even at DEBUG level. */
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

/**
 * Get current token TTL (used by command handlers for display)
 *
 * @return  Token TTL in seconds
 */
int security_get_token_ttl(void) {
    return g_token_ttl;
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

    if (len == 0 || len > 1024)
        return -1;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = text[i];

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
 * Register failed token validation attempt.
 *
 * After 5 consecutive failures blocks further attempts for 30 seconds.
 */
static void register_failed_attempt(time_t now, unsigned short req_id) {

    g_failed_attempts++;

    LOG_SEC(LOG_DEBUG, "req=%04x failed attempts: %d", req_id, g_failed_attempts);

    if (g_failed_attempts >= 5) {
        g_block_until     = now + 30;
        g_failed_attempts = 0;

        LOG_SEC(LOG_WARN,
            "req=%04x Too many invalid tokens, blocking for 30 seconds", req_id);
    }
}

// ============================================================================
// RATE LIMITING
// ============================================================================

/**
 * Apply rate limiting to prevent command spam.
 *
 * Allows up to 5 commands per second.  The burst counter is capped at
 * RATE_LIMIT_MAX (16) to prevent integer overflow on sustained spam --
 * once the cap is reached the counter stays pinned rather than wrapping
 * around and accidentally bypassing the limit check.
 *
 * @return  0 if request allowed, -1 if rate limit exceeded
 */
int security_rate_limit(void) {

    time_t now = time(NULL);

    if (now == g_last_cmd_time) {
        /*
         * Cap the counter to avoid overflow.  Any value > 5 already
         * means "rate limited", so there is no need to let it grow further.
         */
        if (g_cmd_burst < RATE_LIMIT_MAX)
            g_cmd_burst++;

        if (g_cmd_burst > 5) {
            LOG_SEC(LOG_WARN, "Rate limit triggered (burst=%d)", g_cmd_burst);
            return -1;
        }
    } else {
        g_last_cmd_time = now;
        g_cmd_burst     = 1;
    }

    return 0;
}

// ============================================================================
// TOKEN GENERATION (STATELESS)
// ============================================================================

/**
 * Compute the raw hash component for a given (chat_id, timestamp) pair.
 *
 * Combines chat_id, timestamp, compile-time salt, and getrandom-derived
 * runtime salt.  The three-step integer mix (xorshift -> multiply -> xorshift)
 * gives good avalanche behaviour so that adjacent timestamps produce
 * completely different values.
 *
 * @param chat_id  Authorized chat ID
 * @param ts       Timestamp for this token window
 * @return         Unsigned hash value (full 32-bit range)
 */
static unsigned int make_token(long chat_id, time_t ts) {

    unsigned int x = (unsigned int)((unsigned long)chat_id
                                    ^ (unsigned long)ts
                                    ^ TOKEN_SALT
                                    ^ g_runtime_salt);
    x ^= (x >> 16);
    x *= 0x45d9f3bu;
    x ^= (x >> 16);

    return x;
}

// ============================================================================
// PUBLIC TOKEN API
// ============================================================================

/**
 * Generate a new reboot confirmation token.
 *
 * Token is stateless: derived from chat_id, current time, and the
 * getrandom-seeded runtime salt.  Stored internally for replay protection
 * (single-use enforcement in security_validate_reboot_token).
 *
 * @param chat_id  Authorized chat ID requesting the token
 * @param req_id   16-bit request identifier for log correlation
 * @return         6-digit token (000000-999999)
 */
int security_generate_reboot_token(long chat_id, unsigned short req_id) {

    time_t ts = time(NULL);

    unsigned int raw    = make_token(chat_id, ts);
    int final_token     = (int)(((unsigned int)(ts % 1000000) ^ raw) % 1000000u);

    LOG_SEC(LOG_INFO,
        "req=%04x Generated token: chat_id=%ld ts=%ld token=%06d",
        req_id, chat_id, (long)ts, final_token);
    /*
     * Intermediate values (ts_mod, raw hash) are intentionally NOT logged
     * even at DEBUG level -- they would expose information useful for
     * reversing the token algorithm if the log file is compromised.
     */

    g_last_token      = final_token;
    g_last_token_time = ts;

    return final_token;
}

/**
 * Validate a reboot confirmation token.
 *
 * Checks token against all time windows within TTL.  Also enforces:
 *   - Token format (6 digits, non-negative)
 *   - Bruteforce block status
 *   - Replay protection (single-use: token is invalidated on first use)
 *
 * @param chat_id      Authorized chat ID
 * @param input_token  Token supplied by the user
 * @param req_id       16-bit request identifier for log correlation
 * @return             0 if valid, -1 if invalid / expired / replayed
 */
int security_validate_reboot_token(long chat_id, int input_token, unsigned short req_id) {

    if (input_token < 0 || input_token > 999999) {
        LOG_SEC(LOG_WARN,
            "req=%04x Invalid token format: %d (chat_id=%ld)",
            req_id, input_token, chat_id);
        return -1;
    }

    time_t now = time(NULL);

    if (now < g_block_until) {
        LOG_SEC(LOG_WARN, "req=%04x Token validation blocked (bruteforce)", req_id);
        return -1;
    }

    for (int i = 0; i <= g_token_ttl; i++) {

        time_t       ts       = now - i;
        unsigned int raw      = make_token(chat_id, ts);
        int          expected = (int)(((unsigned int)(ts % 1000000) ^ raw) % 1000000u);

        if (expected == input_token) {

            if (input_token == g_last_token &&
                (now - g_last_token_time) <= g_token_ttl) {

                g_last_token = -1;  /* invalidate -- single use */

                LOG_SEC(LOG_INFO,
                    "req=%04x Token accepted: chat_id=%ld token=%06d (age=%d sec)",
                    req_id, chat_id, input_token, i);

                g_failed_attempts = 0;
                g_block_until     = 0;

                return 0;
            }

            LOG_SEC(LOG_WARN,
                "req=%04x Replay or expired token: %06d",
                req_id, input_token);

            register_failed_attempt(now, req_id);
            return -1;
        }
    }

    LOG_SEC(LOG_WARN,
        "req=%04x Invalid token: chat_id=%ld token=%06d",
        req_id, chat_id, input_token);

    register_failed_attempt(now, req_id);
    return -1;
}
