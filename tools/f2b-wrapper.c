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
#include <syslog.h>

// ============================================================================
// CONSTANTS
// ============================================================================

#define F2B_CLIENT          "/usr/bin/fail2ban-client"
#define F2B_WRAPPER_VERSION "1.3"
#define IP_MAX_STRLEN       45   /* Max IPv6 string length (INET6_ADDRSTRLEN) */

// ============================================================================
// IP ADDRESS VALIDATION
// ============================================================================

/**
 * Validate IPv4 address format and check it's a public address.
 *
 * Rejects:
 *   - Malformed IP addresses
 *   - Private addresses (10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16)
 *   - Loopback addresses (127.0.0.0/8)
 *
 * @param ip  String to validate
 * @return    1 if valid public IPv4 address, 0 otherwise
 */
static int is_safe_ip(const char *ip) {
    if (!ip) return 0;

    /* Reject overly long strings before parsing */
    if (strlen(ip) > IP_MAX_STRLEN) return 0;

    struct sockaddr_in sa;
    if (inet_pton(AF_INET, ip, &(sa.sin_addr)) != 1) return 0;

    /* Reject private and loopback addresses */
    uint32_t addr = ntohl(sa.sin_addr.s_addr);

    if ((addr & 0xFF000000) == 0x0A000000) return 0;  /* 10.0.0.0/8     */
    if ((addr & 0xFFF00000) == 0xAC100000) return 0;  /* 172.16.0.0/12  */
    if ((addr & 0xFFFF0000) == 0xC0A80000) return 0;  /* 192.168.0.0/16 */
    if ((addr & 0xFF000000) == 0x7F000000) return 0;  /* 127.0.0.0/8    */

    return 1;
}

// ============================================================================
// USAGE
// ============================================================================

/**
 * Print usage information to stdout.
 */
static void print_usage(void) {
    printf("f2b-wrapper %s - Secure Fail2Ban wrapper for tg-bot\n\n",
           F2B_WRAPPER_VERSION);
    printf("Usage: f2b-wrapper <command> [args...]\n\n");
    printf("Commands:\n");
    printf("  status [jail]          - Show ban status\n");
    printf("  set sshd banip <ip>    - Ban public IP address\n");
    printf("  set sshd unbanip <ip>  - Unban public IP address\n");
    printf("  --version              - Print version\n");
    printf("  --help                 - Print this help\n");
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char *argv[]) {
    // ------------------------------------------------------------------------
    // --version
    // ------------------------------------------------------------------------
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("f2b-wrapper %s\n", F2B_WRAPPER_VERSION);
        return 0;
    }

    // ------------------------------------------------------------------------
    // --help
    // ------------------------------------------------------------------------
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    }

    // ------------------------------------------------------------------------
    // Validate argument count
    // ------------------------------------------------------------------------
    if (argc < 2) {
        print_usage();
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
            syslog(LOG_NOTICE, "executing: status (all jails)");
            execl(F2B_CLIENT, F2B_CLIENT, "status", NULL);
            syslog(LOG_ERR, "exec failed: %m");
            perror("f2b-wrapper: exec failed");
            return 1;
        }

        // Status for specific jail (only sshd is allowed)
        if (argc == 3 && strcmp(argv[2], "sshd") == 0) {
            syslog(LOG_NOTICE, "executing: status sshd");
            execl(F2B_CLIENT, F2B_CLIENT, "status", "sshd", NULL);
            syslog(LOG_ERR, "exec failed: %m");
            perror("f2b-wrapper: exec failed");
            return 1;
        }

        fprintf(stderr, "f2b-wrapper: invalid jail '%s'\n", argv[2]);
        syslog(LOG_WARNING, "rejected invalid jail '%s'", argv[2]);
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
            syslog(LOG_WARNING, "rejected unsupported jail '%s'", argv[2]);
            return 1;
        }

        // Validate action (banip or unbanip)
        if (strcmp(argv[3], "banip") != 0 && strcmp(argv[3], "unbanip") != 0) {
            fprintf(stderr, "f2b-wrapper: invalid action '%s' (use banip or unbanip)\n", argv[3]);
            syslog(LOG_WARNING, "rejected invalid action '%s'", argv[3]);
            return 1;
        }

        // Validate IP address (public only)
        if (!is_safe_ip(argv[4])) {
            fprintf(stderr, "f2b-wrapper: invalid or private IP address '%s'\n", argv[4]);
            syslog(LOG_WARNING, "rejected invalid/private IP '%s'", argv[4]);
            return 1;
        }

        // Log and execute fail2ban-client
        syslog(LOG_NOTICE, "executing: %s %s", argv[3], argv[4]);
        execl(F2B_CLIENT, F2B_CLIENT, "set", "sshd", argv[3], argv[4], NULL);
        syslog(LOG_ERR, "exec failed: %m");
        perror("f2b-wrapper: exec failed");
        return 1;
    }

    // ========================================================================
    // UNKNOWN COMMAND
    // ========================================================================
    fprintf(stderr, "f2b-wrapper: unknown command '%s'\n", argv[1]);
    syslog(LOG_WARNING, "rejected unknown command '%s'", argv[1]);
    return 1;
}
