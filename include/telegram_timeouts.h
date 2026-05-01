/**
 * tg-bot - Telegram bot for system administration
 *
 * telegram_timeouts.h - Telegram HTTP/polling timeout constants
 *
 * Single source of truth for all timeout values in the Telegram
 * communication stack.  The four constants form a strict chain:
 *
 *   CONNECT  <  POLL  <  HTTP  <  CHILD_WAIT
 *
 * Rationale:
 *
 *   CONNECT (5s)
 *     TCP handshake budget.  Should be short — if the host is
 *     unreachable we want to know quickly.  Must be less than
 *     POLL so a slow connect does not eat the whole poll window.
 *
 *   POLL (25s)
 *     Telegram long-polling window.  Telegram holds the connection
 *     open for up to this many seconds waiting for a new message,
 *     then returns an empty result array.  Must be less than HTTP
 *     so curl does not time out a legitimate idle poll.
 *
 *   HTTP (35s)
 *     Total curl request budget (CURLOPT_TIMEOUT).  Covers the
 *     full round-trip: connect + waiting + transfer.  Must be
 *     greater than POLL so idle long-poll responses are not
 *     killed prematurely.
 *
 *   CHILD_WAIT (38 000 ms)
 *     How long the parent process waits for the forked curl child
 *     before sending SIGKILL.  Must exceed HTTP so curl has a
 *     chance to return its own error before we forcibly kill it.
 *     The 3-second margin is intentional headroom.
 *
 * ⚠️  The _Static_assert checks below enforce the chain at compile
 *     time.  If you change any value, the compiler will tell you
 *     if the invariant is broken.
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#ifndef TELEGRAM_TIMEOUTS_H
#define TELEGRAM_TIMEOUTS_H

/* TCP connection timeout (seconds) */
#define TG_CONNECT_TIMEOUT_SEC    5

/* Telegram long-polling window (seconds, passed as ?timeout= in URL) */
#define TG_POLL_TIMEOUT_SEC       25

/* Total curl request timeout (seconds, CURLOPT_TIMEOUT) */
#define TG_HTTP_TIMEOUT_SEC       35

/* Parent waits for forked curl child (milliseconds) */
#define TG_CHILD_WAIT_TIMEOUT_MS  38000

/* -----------------------------------------------------------------------
 * Compile-time invariant checks.
 *
 * These fire as build errors if the timeout chain is accidentally broken,
 * e.g. someone bumps POLL above HTTP without adjusting the others.
 * ----------------------------------------------------------------------- */
_Static_assert(TG_CONNECT_TIMEOUT_SEC < TG_POLL_TIMEOUT_SEC,
    "TG_CONNECT_TIMEOUT_SEC must be < TG_POLL_TIMEOUT_SEC");

_Static_assert(TG_POLL_TIMEOUT_SEC < TG_HTTP_TIMEOUT_SEC,
    "TG_POLL_TIMEOUT_SEC must be < TG_HTTP_TIMEOUT_SEC");

_Static_assert(TG_HTTP_TIMEOUT_SEC * 1000 < TG_CHILD_WAIT_TIMEOUT_MS,
    "TG_HTTP_TIMEOUT_SEC * 1000 must be < TG_CHILD_WAIT_TIMEOUT_MS");

#endif /* TELEGRAM_TIMEOUTS_H */
