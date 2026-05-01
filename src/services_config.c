/**
 * tg-bot - Telegram bot for system administration
 *
 * services_config.c - Service table definition
 *
 * This is the ONLY file that needs to be edited when adding,
 * removing, or renaming a monitored service.
 *
 * Each entry has three fields:
 *   alias    — name used in bot commands (/logs <alias>, /services)
 *   unit     — systemd unit name (passed to systemctl / journalctl)
 *   display  — label shown in /services output
 *
 * Example — adding nginx:
 *   {"nginx", "nginx", "Nginx"},
 *
 * Example — service where alias differs from unit name:
 *   {"shadowsocks", "shadowsocks-libev", "Shadowsocks"},
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "services_config.h"

// ============================================================================
// SERVICE TABLE
// ============================================================================

const service_def_t g_services[] = {
    /*  alias           unit                 display        */
    {  "ssh",          "ssh",               "SSH"          },
    {  "shadowsocks",  "shadowsocks-libev", "Shadowsocks"  },
    {  "mtg",          "mtg",               "MTG"          },
};

const int g_services_count = (int)(sizeof(g_services) / sizeof(g_services[0]));
