/**
 * tg-bot - Telegram bot for system administration
 * cmd_ssconfig.h - /ssconfig command handler API
 * MIT License - Copyright (c) 2026 ironmist45
 */
#ifndef CMD_SSCONFIG_H
#define CMD_SSCONFIG_H

#include "commands.h"

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Generate Shadowsocks + Cloak client config files (V2 handler)
 *
 * Reads server parameters from CLOAK_SS_CONFIG (config.json) and
 * CLOAK_CK_CONFIG (ckserver.json), combines with CLOAK_PUBLIC_KEY
 * from bot config and auto-detected server IPv4 (via hostname -I).
 *
 * Writes two JSON files to UPLOAD_DIR:
 *   cloak-client-<timestamp>.json  — Cloak client config
 *   ss-client-<timestamp>.json     — Shadowsocks client config
 *
 * Fields read from config.json:
 *   server_port, password, method
 *
 * Fields read from ckserver.json:
 *   BypassUID (first array element), RedirAddr
 *
 * Requirements:
 *   - CLOAK_PUBLIC_KEY must be set in bot config
 *   - UPLOAD_ENABLED=yes and UPLOAD_DIR must be configured
 *   - Config files must be readable by tg-bot user
 *
 * If CLOAK_PUBLIC_KEY or UPLOAD_ENABLED is missing, returns a
 * plain-text hint explaining how to enable the feature.
 *
 * @param ctx  Command context
 * @return     0 on success, -1 on error
 */
int cmd_ssconfig_v2(command_ctx_t *ctx);

#endif /* CMD_SSCONFIG_H */
