/**
 * tg-bot - Telegram bot for system administration
 *
 * services_config.h - Shared service definitions
 *
 * Single source of truth for the list of monitored services.
 * Used by both services.c (/services command) and logs.c (/logs command).
 *
 * To add a new service: edit services_config.c only.
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#ifndef SERVICES_CONFIG_H
#define SERVICES_CONFIG_H

// ============================================================================
// SERVICE DEFINITION
// ============================================================================

/**
 * Describes a single monitored service.
 *
 * Fields:
 *   alias    — user-facing name used in bot commands
 *              e.g. "shadowsocks" in "/logs shadowsocks"
 *   unit     — systemd unit name passed to systemctl / journalctl
 *              e.g. "shadowsocks-libev"
 *   display  — human-readable label shown in /services output
 *              e.g. "Shadowsocks"
 */
typedef struct {
    const char *alias;    /* user-facing name in bot commands  */
    const char *unit;     /* systemd unit name                 */
    const char *display;  /* label in /services output         */
} service_def_t;

// ============================================================================
// GLOBAL SERVICE TABLE (defined in services_config.c)
// ============================================================================

extern const service_def_t g_services[];
extern const int            g_services_count;

#endif /* SERVICES_CONFIG_H */
