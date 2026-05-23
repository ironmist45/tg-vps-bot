/**
 * tg-bot - Telegram bot for system administration
 *
 * sd_notify.h - Minimal systemd service notification
 *
 * Implements sd_notify(3) without linking against libsystemd.
 * Communicates with systemd via the NOTIFY_SOCKET Unix datagram socket
 * that systemd provides to the service process.
 *
 * Supports two notification strings:
 *
 *   "READY=1"      — service has finished initialising and is ready
 *                    to handle requests (required for Type=notify)
 *
 *   "WATCHDOG=1"   — service is alive and functioning correctly;
 *                    must be sent at least once per WatchdogSec/2
 *
 * If NOTIFY_SOCKET is not set (e.g. running outside systemd, in CI,
 * or with Type=simple), all calls are silent no-ops — safe to call
 * unconditionally.
 *
 * Usage:
 *   // After full initialisation:
 *   sd_notify_ready();
 *
 *   // In the main loop (every iteration):
 *   sd_notify_watchdog();
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */
#ifndef SD_NOTIFY_H
#define SD_NOTIFY_H

#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

/* -------------------------------------------------------------------------
 * sd_notify_send - send a notification string to systemd.
 *
 * Internal function used by sd_notify_ready() and sd_notify_watchdog().
 * Returns 1 on success, 0 if NOTIFY_SOCKET is not set, -1 on error.
 * ------------------------------------------------------------------------- */
static inline int sd_notify_send(const char *msg) {
    const char *socket_path = getenv("NOTIFY_SOCKET");

    /* Not running under systemd notify — silent no-op */
    if (!socket_path || socket_path[0] == '\0')
        return 0;

    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOG_SYS(LOG_WARN, "sd_notify: socket() failed: errno=%d", errno);
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    /*
     * NOTIFY_SOCKET may be an abstract socket (starts with '@') or a
     * filesystem path. Abstract sockets use a null byte as the first
     * character in sun_path.
     */
    if (socket_path[0] == '@') {
        /* Abstract socket: skip '@', write into sun_path[1..] */
        if (strlen(socket_path + 1) >= sizeof(addr.sun_path) - 1) {
            LOG_SYS(LOG_WARN, "sd_notify: NOTIFY_SOCKET path too long");
            close(fd);
            return -1;
        }
        addr.sun_path[0] = '\0';
        strncpy(addr.sun_path + 1, socket_path + 1, sizeof(addr.sun_path) - 2);
    } else {
        /* Filesystem socket */
        if (strlen(socket_path) >= sizeof(addr.sun_path)) {
            LOG_SYS(LOG_WARN, "sd_notify: NOTIFY_SOCKET path too long");
            close(fd);
            return -1;
        }
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    }

    ssize_t sent = sendto(fd, msg, strlen(msg), MSG_NOSIGNAL,
                          (struct sockaddr *)&addr, sizeof(addr));
    close(fd);

    if (sent < 0) {
        LOG_SYS(LOG_WARN, "sd_notify: sendto() failed: errno=%d", errno);
        return -1;
    }

    return 1;
}

/* -------------------------------------------------------------------------
 * sd_notify_ready - tell systemd the service is fully initialised.
 *
 * Call once after all modules are initialised and the bot is ready
 * to accept commands. Required when Type=notify in the unit file.
 * ------------------------------------------------------------------------- */
static inline void sd_notify_ready(void) {
    int rc = sd_notify_send("READY=1");
    if (rc > 0)
        LOG_SYS(LOG_INFO, "sd_notify: READY=1 sent");
}

/* -------------------------------------------------------------------------
 * sd_notify_watchdog - reset the systemd watchdog timer.
 *
 * Call at least once per WatchdogSec/2 seconds (i.e. at least every
 * 30 seconds when WatchdogSec=60 in the unit file).
 * Safe to call on every main loop iteration — the overhead is negligible
 * when NOTIFY_SOCKET is not set (immediate return after getenv()).
 * ------------------------------------------------------------------------- */
static inline void sd_notify_watchdog(void) {
    if (sd_notify_send("WATCHDOG=1") < 0)
        LOG_SYS(LOG_WARN, "sd_notify: WATCHDOG=1 failed");
}

#endif /* SD_NOTIFY_H */
