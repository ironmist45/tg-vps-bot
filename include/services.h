/**
 * tg-bot - Telegram bot for system administration
 * services.h - Systemd service status monitoring
 * MIT License - Copyright (c) 2026
 */

#ifndef SERVICES_H
#define SERVICES_H

#include <stddef.h>

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Get formatted status of all whitelisted services
 * 
 * Queries systemd for the status of predefined services and formats
 * the result for Telegram display.
 * 
 * Output format (Markdown):
 *   *🧩 SERVICES*
 *   
 *   ```
 *   SSH             : 🟢 UP
 *   Shadowsocks     : 🔴 DOWN
 *   MTG             : 🟢 UP
 *   ```
 * 
 * Status indicators:
 *   🟢 UP        - service is active
 *   🔴 DOWN      - service is inactive
 *   🟡 FAIL      - service failed
 *   🟡 STARTING  - service is activating
 *   ⚪ UNKNOWN   - status could not be determined
 * 
 * @param buffer  Output buffer for formatted response
 * @param size    Size of output buffer
 * @param req_id  16-bit request identifier for log correlation
 * @return        0 on success, -1 on error
 */
int services_get_status(char *buffer, size_t size, unsigned short req_id);

#endif // SERVICES_H
