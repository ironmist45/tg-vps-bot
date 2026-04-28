/**
 * tg-bot - Telegram bot for system administration
 * f2b-wrapper.c - Secure Fail2Ban wrapper for tg-bot
 * 
 * Provides a restricted interface to fail2ban-client for use by the bot.
 * Only allows:
 *   - status          (all jails)
 *   - status sshd     (specific jail)
 *   - set sshd banip <ip>
 *   - set sshd unbanip <ip>
 * 
 * All commands require root privileges.
 * IP addresses are validated before being passed to fail2ban-client.
 * Uses full path to fail2ban-client for sudo compatibility.
 * 
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>

// ============================================================================
// CONSTANTS
// ============================================================================

#define F2B_CLIENT "/usr/bin/fail2ban-client"

// ============================================================================
// IP ADDRESS VALIDATION
// ============================================================================

/**
 * Validate IPv4 address format
 * 
 * @param ip  String to validate
 * @return    1 if valid IPv4 address, 0 otherwise
 */
static int is_safe_ip(const char *ip) {
    if (!ip) return 0;

    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip, &(sa.sin_addr)) == 1;
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char *argv[]) {
    // ------------------------------------------------------------------------
    // Validate argument count
    // ------------------------------------------------------------------------
    if (argc < 2) {
        fprintf(stderr, "Usage: f2b-wrapper <command> [args...]\n");
        fprintf(stderr, "Commands:\n");
        fprintf(stderr, "  status [jail]          - Show ban status\n");
        fprintf(stderr, "  set sshd banip <ip>    - Ban IP address\n");
        fprintf(stderr, "  set sshd unbanip <ip>  - Unban IP address\n");
        return 1;
    }

    // ------------------------------------------------------------------------
    // Check root privileges (required for fail2ban-client)
    // ------------------------------------------------------------------------
    if (geteuid() != 0) {
        fprintf(stderr, "f2b-wrapper: must be run as root\n");
        return 1;
    }

    // ========================================================================
    // STATUS COMMAND
    // ========================================================================
    if (strcmp(argv[1], "status") == 0) {
        // Generic status (all jails)
        if (argc == 2) {
            execl(F2B_CLIENT, F2B_CLIENT, "status", NULL);
            perror("f2b-wrapper: exec failed");
            return 1;
        }

        // Status for specific jail (only sshd is allowed)
        if (argc == 3 && strcmp(argv[2], "sshd") == 0) {
            execl(F2B_CLIENT, F2B_CLIENT, "status", "sshd", NULL);
            perror("f2b-wrapper: exec failed");
            return 1;
        }

        fprintf(stderr, "f2b-wrapper: invalid jail '%s'\n", argv[2]);
        return 1;
    }

    // ========================================================================
    // SET COMMAND (ban/unban)
    // ========================================================================
    if (strcmp(argv[1], "set") == 0) {
        // Validate argument count
        if (argc != 5) {
            fprintf(stderr, "f2b-wrapper: set requires 3 arguments (jail action ip)\n");
            return 1;
        }

        // Only sshd jail is allowed
        if (strcmp(argv[2], "sshd") != 0) {
            fprintf(stderr, "f2b-wrapper: only 'sshd' jail is supported\n");
            return 1;
        }

        // Validate action (banip or unbanip)
        if (strcmp(argv[3], "banip") != 0 && strcmp(argv[3], "unbanip") != 0) {
            fprintf(stderr, "f2b-wrapper: invalid action '%s' (use banip or unbanip)\n", argv[3]);
            return 1;
        }

        // Validate IP address
        if (!is_safe_ip(argv[4])) {
            fprintf(stderr, "f2b-wrapper: invalid IP address '%s'\n", argv[4]);
            return 1;
        }

        // Execute fail2ban-client
        execl(F2B_CLIENT, F2B_CLIENT, "set", "sshd", argv[3], argv[4], NULL);
        perror("f2b-wrapper: exec failed");
        return 1;
    }

    // ========================================================================
    // UNKNOWN COMMAND
    // ========================================================================
    fprintf(stderr, "f2b-wrapper: unknown command '%s'\n", argv[1]);
    return 1;
}
