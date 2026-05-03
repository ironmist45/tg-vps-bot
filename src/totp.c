/**
 * tg-bot - Telegram bot for system administration
 *
 * totp.c - TOTP (Time-based One-Time Password) implementation
 *
 * Implements RFC 6238 (TOTP) over RFC 4226 (HOTP) using HMAC-SHA1
 * via OpenSSL which is already statically linked in the project.
 *
 * Algorithm summary (RFC 6238 + RFC 4226):
 *
 *   1. counter  = floor(unix_time / TOTP_STEP)         [RFC 6238]
 *   2. msg      = counter as 8-byte big-endian uint64   [RFC 4226]
 *   3. hmac     = HMAC-SHA1(base32_decode(secret), msg) [RFC 2104]
 *   4. offset   = hmac[19] & 0x0f                       [RFC 4226 §5.3]
 *   5. truncated= (hmac[offset..offset+3] & 0x7fffffff) [RFC 4226 §5.3]
 *   6. code     = truncated % 10^TOTP_DIGITS             [RFC 4226 §5.3]
 *
 * Test vectors (RFC 6238 Appendix B, SHA-1):
 *   secret = "12345678901234567890" (ASCII, base32: GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ)
 *   T=59          → 94287082
 *   T=1111111109  → 07081804
 *   T=1111111111  → 14050471
 *   T=1234567890  → 89005924
 *   T=2000000000  → 69279037
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#define _GNU_SOURCE

#include "totp.h"
#include "logger.h"

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include <stdint.h>

#include <openssl/hmac.h>
#include <openssl/evp.h>

// ============================================================================
// BASE32 DECODING (RFC 4648)
// ============================================================================

/*
 * Base32 alphabet: A-Z = 0..25, 2-7 = 26..31
 * Returns -1 for invalid characters (including padding '=').
 */
static int base32_char_value(char c) {
    c = toupper((unsigned char)c);
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

/**
 * Decode base32 string into raw bytes (RFC 4648).
 *
 * Processes input in 8-character groups (5 bytes output each).
 * Padding ('=') and whitespace are silently ignored.
 *
 * @return  Number of decoded bytes, or -1 on invalid input
 */
int base32_decode(const char *encoded, uint8_t *decoded, size_t decoded_size) {
    if (!encoded || !decoded || decoded_size == 0)
        return -1;

    size_t out_idx = 0;
    int    buf     = 0;   /* bit accumulator          */
    int    bits    = 0;   /* bits currently in buffer */

    for (size_t i = 0; encoded[i]; i++) {
        char c = encoded[i];

        /* Skip padding and whitespace */
        if (c == '=' || c == ' ' || c == '\n' || c == '\r')
            continue;

        int val = base32_char_value(c);
        if (val < 0) {
            LOG_SYS(LOG_WARN, "totp: invalid base32 character '%c' at pos %zu", c, i);
            return -1;
        }

        /* Accumulate 5 bits */
        buf  = (buf << 5) | val;
        bits += 5;

        /* Emit a full byte */
        if (bits >= 8) {
            bits -= 8;
            if (out_idx >= decoded_size) {
                LOG_SYS(LOG_WARN, "totp: base32 output buffer too small");
                return -1;
            }
            decoded[out_idx++] = (uint8_t)((buf >> bits) & 0xFF);
        }
    }

    return (int)out_idx;
}

// ============================================================================
// HMAC-SHA1 (RFC 2104 via OpenSSL)
// ============================================================================

/*
 * Compute HMAC-SHA1(key, msg) → 20-byte digest.
 *
 * Uses the OpenSSL HMAC API which is already available via the
 * statically linked libssl/libcrypto in this project.
 *
 * @return  0 on success, -1 on OpenSSL error
 */
static int hmac_sha1(const uint8_t *key,  size_t key_len,
                     const uint8_t *msg,  size_t msg_len,
                     uint8_t        digest[20])
{
    unsigned int digest_len = 20;

    if (!HMAC(EVP_sha1(),
              key, (int)key_len,
              msg, (int)msg_len,
              digest, &digest_len)) {
        LOG_SYS(LOG_ERROR, "totp: HMAC-SHA1 failed");
        return -1;
    }

    return 0;
}

// ============================================================================
// HOTP (RFC 4226)
// ============================================================================

/*
 * Compute HOTP(secret, counter) → N-digit code.
 *
 * RFC 4226 §5.3 dynamic truncation:
 *   offset   = hmac[19] & 0x0f
 *   P        = hmac[offset..offset+3] as big-endian uint32, MSB cleared
 *   code     = P % 10^digits
 *
 * @param secret_b32  Base32-encoded HMAC key
 * @param counter     8-byte counter value
 * @param code        Output: computed OTP
 * @return            0 on success, -1 on error
 */
static int hotp_compute(const char *secret_b32,
                        uint64_t    counter,
                        int        *code)
{
    /* Decode secret */
    uint8_t key[64];
    int key_len = base32_decode(secret_b32, key, sizeof(key));
    if (key_len <= 0) {
        LOG_SYS(LOG_WARN, "totp: failed to decode secret");
        return -1;
    }

    /*
     * Encode counter as 8-byte big-endian (RFC 4226 requirement).
     * The phone and server must agree on byte order — big-endian is mandated.
     */
    uint8_t msg[8];
    for (int i = 7; i >= 0; i--) {
        msg[i] = (uint8_t)(counter & 0xFF);
        counter >>= 8;
    }

    /* Compute HMAC-SHA1 */
    uint8_t digest[20];
    if (hmac_sha1(key, (size_t)key_len, msg, sizeof(msg), digest) != 0)
        return -1;

    /* Dynamic truncation (RFC 4226 §5.3) */
    int offset = digest[19] & 0x0f;

    uint32_t truncated =
        ((uint32_t)(digest[offset]     & 0x7f) << 24) |
        ((uint32_t)(digest[offset + 1] & 0xff) << 16) |
        ((uint32_t)(digest[offset + 2] & 0xff) <<  8) |
        ((uint32_t)(digest[offset + 3] & 0xff));

    /* Compute modulus for requested digit count */
    uint32_t modulus = 1;
    for (int i = 0; i < TOTP_DIGITS; i++)
        modulus *= 10;

    *code = (int)(truncated % modulus);
    return 0;
}

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Generate a TOTP code for a given Unix timestamp (RFC 6238).
 */
int totp_generate(const char *secret_b32, time_t t, int *code) {
    if (!secret_b32 || !code)
        return -1;

    uint64_t counter = (uint64_t)t / TOTP_STEP;
    return hotp_compute(secret_b32, counter, code);
}

/**
 * Verify a TOTP code against the current time.
 *
 * Checks TOTP_WINDOW steps in each direction to handle:
 *   - Clock skew between phone and server
 *   - User entering code just as the window expires
 */
int totp_verify(const char *secret_b32, int input_code) {
    if (!secret_b32)
        return -1;

    if (input_code < 0 || input_code >= 1000000) {
        LOG_SYS(LOG_WARN, "totp: invalid code format: %d", input_code);
        return -1;
    }

    time_t now = time(NULL);

    for (int delta = -TOTP_WINDOW; delta <= TOTP_WINDOW; delta++) {
        time_t t = now + (time_t)(delta * TOTP_STEP);
        int expected = 0;

        if (totp_generate(secret_b32, t, &expected) != 0)
            return -1;

        if (expected == input_code) {
            LOG_SYS(LOG_DEBUG,
                "totp: code accepted (delta=%d step)", delta);
            return 0;
        }
    }

    LOG_SYS(LOG_WARN, "totp: invalid code: %06d", input_code);
    return -1;
}

/**
 * Validate a base32-encoded secret string.
 */
int totp_validate_secret(const char *secret_b32) {
    if (!secret_b32 || secret_b32[0] == '\0')
        return -1;

    size_t len = strlen(secret_b32);
    if (len > TOTP_SECRET_MAX)
        return -1;

    for (size_t i = 0; i < len; i++) {
        char c = secret_b32[i];
        /* Allow base32 alphabet + padding */
        if (base32_char_value(c) < 0 && c != '=')
            return -1;
    }

    /* Try to actually decode — catches malformed padding etc. */
    uint8_t tmp[64];
    if (base32_decode(secret_b32, tmp, sizeof(tmp)) <= 0)
        return -1;

    return 0;
}

/**
 * Generate an otpauth:// URI for QR code or manual entry.
 *
 * Example output:
 *   otpauth://totp/tg-bot?secret=JBSWY3DPEHPK3PXP&issuer=tg-bot
 */
int totp_uri(const char *secret_b32, const char *label,
             const char *issuer, char *buf, size_t size)
{
    if (!secret_b32 || !label || !issuer || !buf || size == 0)
        return -1;

    int written = snprintf(buf, size,
        "otpauth://totp/%s?secret=%s&issuer=%s&algorithm=SHA1&digits=%d&period=%d",
        label, secret_b32, issuer, TOTP_DIGITS, TOTP_STEP);

    if (written < 0 || (size_t)written >= size) {
        LOG_SYS(LOG_WARN, "totp: URI buffer too small");
        return -1;
    }

    return 0;
}
