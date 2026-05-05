/**
 * tg-bot - Telegram bot for system administration
 *
 * security.c - Access control and token-based confirmation
 *
 * Provides:
 *   - Single-user access control (chat_id validation)
 *   - Input sanitization (control character filtering)
 *   - Rate limiting (prevent command spam)
 *   - Stateless reboot tokens (confirmation for critical actions)
 *   - Bruteforce protection (temporary block after failed attempts)
 *   - TOTP status reporting in security_init()
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "security.h"
#include "logger.h"
#include "config.h"
#include "telegram.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <sys/random.h>

/*
 * Compile-time token salt — NOT a cryptographic secret.
 * Tokens are protected by runtime salt (generated via getrandom).
 * Real security is enforced by chat_id validation.
 * Purpose: prevent accidental execution, add confirmation step.
 */
#define TOKEN_SALT 0x5F3759DF

/*
 * Maximum burst count for rate limiting.
 * Capped to prevent integer overflow on sustained spam.
 */
#define RATE_LIMIT_MAX 16

/* Input validation */
#define SECURITY_TEXT_MAX_LEN        1024

/* Bruteforce protection */
#define SECURITY_MAX_FAILED_ATTEMPTS 5
#define SECURITY_BLOCK_DURATION_SEC  30

/* Rate limiting — max commands per second before blocking */
#define SECURITY_RATE_BURST_MAX      5

/* Token TTL upper bound */
#define SECURITY_TOKEN_TTL_MAX       3600

// ============================================================================
// INTERNAL STATE
// ============================================================================

static long          g_allowed_chat_id = 0;
static int           g_token_ttl       = 60;
static unsigned int  g_runtime_salt    = 0;

/* Replay protection */
static int    g_last_token      = -1;
static time_t g_last_token_time = 0;

/* Bruteforce protection */
static int    g_failed_attempts = 0;
static time_t g_block_until     = 0;

/* Rate limiting */
static time_t g_last_cmd_time = 0;
static int    g_cmd_burst     = 0;

// ============================================================================
// INITIALIZATION
// ============================================================================

void security_init(void) {
    unsigned int salt = 0;

    /*
     * getrandom(2) blocks until the kernel entropy pool is initialised
     * (only relevant at very early boot) and cannot be interrupted by
     * signals for requests <= 256 bytes.
     */
    ssize_t n = getrandom(&salt, sizeof(salt), 0);

    if (n != (ssize_t)sizeof(salt)) {
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

    if (g_cfg.totp_secret[0] != '\0') {
        LOG_SEC(LOG_INFO, "Security initialized (TOTP 2FA enabled, getrandom salt)");
    } else {
        LOG_SEC(LOG_INFO, "Security initialized (stateless tokens, getrandom salt)");
    }
    /* Do NOT log the salt value — even at DEBUG level. */
}

// ============================================================================
// CONFIGURATION
// ============================================================================

void security_set_allowed_chat(long chat_id) {
    g_allowed_chat_id = chat_id;
}

void security_set_token_ttl(int ttl) {
    if (ttl > 0 && ttl <= SECURITY_TOKEN_TTL_MAX)
        g_token_ttl = ttl;
}

int security_get_token_ttl(void) {
    return g_token_ttl;
}

// ============================================================================
// ACCESS CONTROL
// ============================================================================

int security_is_allowed_chat(long chat_id) {
    if (g_allowed_chat_id == 0) {
        LOG_SEC(LOG_ERROR, "allowed_chat_id not initialized");
        return 0;
    }
    return chat_id == g_allowed_chat_id;
}

int security_validate_text(const char *text) {
    if (!text)
        return -1;

    size_t len = strlen(text);
    if (len == 0 || len > SECURITY_TEXT_MAX_LEN)
        return -1;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = text[i];
        if (c < 32 && c != '\n' && c != '\r' && c != '\t')
            return -1;
    }
    return 0;
}

int security_check_access(long chat_id, const char *cmd, unsigned short req_id) {
    int allowed = security_is_allowed_chat(chat_id);

    LOG_SEC(LOG_INFO,
        "poll=%04x req=%04x ACCESS CHECK: chat_id=%ld cmd=%s result=%s",
        telegram_get_poll_id(), req_id, chat_id, cmd,
        allowed ? "ALLOW" : "DENY");

    if (!allowed) {
        LOG_STATE(LOG_WARN,
            "ACCESS DENIED: req=%04x cmd=%s chat_id=%ld",
            req_id, cmd ? cmd : "NULL", chat_id);
        return -1;
    }
    return 0;
}

// ============================================================================
// BRUTEFORCE PROTECTION
// ============================================================================

static void register_failed_attempt(time_t now, unsigned short req_id) {
    g_failed_attempts++;

    LOG_SEC(LOG_DEBUG, "req=%04x failed attempts: %d", req_id, g_failed_attempts);

    if (g_failed_attempts >= SECURITY_MAX_FAILED_ATTEMPTS) {
        g_block_until     = now + SECURITY_BLOCK_DURATION_SEC;
        g_failed_attempts = 0;
        LOG_SEC(LOG_WARN,
            "req=%04x Too many invalid tokens, blocking for %d seconds",
            req_id, SECURITY_BLOCK_DURATION_SEC);
    }
}

// ============================================================================
// RATE LIMITING
// ============================================================================

int security_rate_limit(void) {
    time_t now = time(NULL);

    if (now == g_last_cmd_time) {
        if (g_cmd_burst < RATE_LIMIT_MAX)
            g_cmd_burst++;

        if (g_cmd_burst > SECURITY_RATE_BURST_MAX) {
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

/*
 * Compute the raw hash component for a (chat_id, timestamp) pair.
 *
 * Combines chat_id, timestamp, compile-time TOKEN_SALT, and the
 * getrandom-derived runtime salt. Three-step integer mix gives good
 * avalanche behaviour so adjacent timestamps produce different values.
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
 * Generate a reboot confirmation token (stateless, single-use).
 *
 * Derived from chat_id, current time, and getrandom-seeded runtime salt.
 * Stored internally for replay protection.
 *
 * @return  6-digit token (000000–999999)
 */
int security_generate_reboot_token(long chat_id, unsigned short req_id) {
    time_t ts = time(NULL);

    unsigned int raw  = make_token(chat_id, ts);
    int final_token   = (int)(((unsigned int)(ts % 1000000) ^ raw) % 1000000u);

    LOG_SEC(LOG_INFO,
        "req=%04x Generated token: chat_id=%ld ts=%ld token=%06d",
        req_id, chat_id, (long)ts, final_token);
    /*
     * Intermediate values (ts_mod, raw hash) are intentionally NOT logged
     * even at DEBUG — they would expose information useful for reversing
     * the token algorithm if the log file is compromised.
     */

    g_last_token      = final_token;
    g_last_token_time = ts;

    return final_token;
}

/**
 * Validate a reboot confirmation token.
 *
 * Checks all time windows within TTL. Enforces:
 *   - Token format (6 digits, non-negative)
 *   - Bruteforce block status
 *   - Replay protection (single-use: invalidated on first successful use)
 *
 * @return  0 if valid, -1 if invalid / expired / replayed / blocked
 */
int security_validate_reboot_token(long chat_id, int input_token,
                                   unsigned short req_id) {
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

                g_last_token = -1;  /* invalidate — single use */

                LOG_SEC(LOG_INFO,
                    "req=%04x Token accepted: chat_id=%ld token=%06d (age=%d sec)",
                    req_id, chat_id, input_token, i);

                g_failed_attempts = 0;
                g_block_until     = 0;
                return 0;
            }

            LOG_SEC(LOG_WARN,
                "req=%04x Replay or expired token: %06d", req_id, input_token);

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