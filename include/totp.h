/**
 * tg-bot - Telegram bot for system administration
 *
 * totp.h - TOTP (Time-based One-Time Password) implementation
 *
 * Implements RFC 6238 (TOTP) and RFC 4226 (HOTP) using
 * HMAC-SHA1 via OpenSSL (already statically linked).
 *
 * Compatible with Google Authenticator, Authy, and any
 * standard TOTP application.
 *
 * Usage:
 *   1. Generate a secret:  openssl rand -base32 20
 *   2. Add to config:      TOTP_SECRET=JBSWY3DPEHPK3PXP
 *   3. Get URI:            totp_uri(secret, "tg-bot", buf, size)
 *   4. Scan QR or paste URI into authenticator app
 *   5. Verify codes:       totp_verify(secret, user_input)
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#ifndef TOTP_H
#define TOTP_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

// ============================================================================
// CONSTANTS
// ============================================================================

/* TOTP time step in seconds (standard: 30) */
#define TOTP_STEP       30

/* Number of digits in generated code (standard: 6) */
#define TOTP_DIGITS     6

/*
 * Verification window — number of steps checked in each direction.
 * Window of 1 means codes from [now-30s .. now+30s] are accepted.
 * Compensates for clock skew between phone and server.
 */
#define TOTP_WINDOW     1

/* Maximum length of a base32-encoded secret (including null terminator) */
#define TOTP_SECRET_MAX 64

/* Maximum length of an otpauth:// URI */
#define TOTP_URI_MAX    256

// ============================================================================
// CORE API
// ============================================================================

/**
 * Generate a TOTP code for a given timestamp.
 *
 * Implements RFC 6238: counter = floor(t / TOTP_STEP),
 * code = HOTP(secret, counter) truncated to TOTP_DIGITS digits.
 *
 * @param secret_b32  Base32-encoded secret (e.g. "JBSWY3DPEHPK3PXP")
 * @param t           Unix timestamp (pass time(NULL) for current code)
 * @param code        Output: generated code (0 .. 10^TOTP_DIGITS - 1)
 * @return            0 on success, -1 on error (invalid secret, OpenSSL fail)
 */
int totp_generate(const char *secret_b32, time_t t, int *code);

/**
 * Verify a TOTP code against the current time.
 *
 * Checks TOTP_WINDOW steps before and after the current 30-second
 * window to compensate for clock skew and input delay.
 *
 * @param secret_b32  Base32-encoded secret
 * @param input_code  Code provided by the user (0 .. 999999)
 * @return            0 if valid, -1 if invalid or expired
 */
int totp_verify(const char *secret_b32, int input_code);

// ============================================================================
// SETUP HELPERS
// ============================================================================

/**
 * Validate a base32-encoded secret string.
 *
 * Checks that the string contains only valid base32 characters
 * (A-Z, 2-7, optional '=' padding) and fits within TOTP_SECRET_MAX.
 *
 * @param secret_b32  Secret to validate
 * @return            0 if valid, -1 if invalid
 */
int totp_validate_secret(const char *secret_b32);

/**
 * Generate an otpauth:// URI for QR code generation or manual entry.
 *
 * Format: otpauth://totp/<label>?secret=<SECRET>&issuer=<issuer>
 *
 * The URI can be:
 *   - Pasted into Google Authenticator / Authy manually
 *   - Converted to a QR code via any online tool or `qrencode`
 *
 * @param secret_b32  Base32-encoded secret
 * @param label       Account label shown in the authenticator app
 * @param issuer      Issuer name shown in the authenticator app
 * @param buf         Output buffer for the URI
 * @param size        Size of output buffer (use TOTP_URI_MAX)
 * @return            0 on success, -1 on error
 */
int totp_uri(const char *secret_b32, const char *label,
             const char *issuer, char *buf, size_t size);

// ============================================================================
// INTERNAL: BASE32
// ============================================================================

/**
 * Decode a base32-encoded string into raw bytes.
 *
 * Follows RFC 4648. Padding ('=') is optional.
 * Input characters are case-insensitive.
 *
 * @param encoded       Null-terminated base32 string
 * @param decoded       Output buffer for raw bytes
 * @param decoded_size  Size of output buffer
 * @return              Number of decoded bytes, or -1 on error
 */
int base32_decode(const char *encoded, uint8_t *decoded, size_t decoded_size);

#endif /* TOTP_H */
