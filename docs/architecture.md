# Architecture

This document describes the structure and organization of tg-bot.

## Project Layout

```
tg-bot/
├── README.md                    # 📘 project documentation (setup, usage, architecture)
├── Makefile                     # ⚙️ build system (compile, clean, targets)
├── .gitignore                   # 🚫 ignored files (build artifacts, logs, etc.)
├── tg-bot.service               # 🔧 systemd unit file (Type=notify, WatchdogSec=60)
├── tg-bot.logrotate             # 🔧 logrotate config (daily rotation, SIGUSR1 reopen)
│
├── .github/
│   └── workflows/
│       └── build-static.yml     # 🔧 CI/CD pipeline (fully static build)
│
├── config/
│   └── config.example.conf      # 🔧 example configuration (env template)
│
├── include/                     # 📂 public headers (module APIs)
│   ├── version.h                # 🔹 version and build information
│   ├── build_info.h             # 🔹 generated build info (commit, date, build number)
│   ├── cli.h                    # 🔹 command-line interface parsing
│   ├── config.h                 # 🔹 config loader API (parsing, reload, TOTP secret)
│   ├── logger.h                 # 🔹 logging system API (levels, macros, early buffer)
│   ├── lifecycle.h              # 🔹 process lifecycle (signals, shutdown, reboot)
│   ├── environment.h            # 🔹 runtime environment diagnostics
│   ├── exec.h                   # 🔹 execution API (command runner with timeout)
│   ├── metrics.h                # 🔹 bot usage metrics and statistics
│   ├── sd_notify.h              # 🔹 minimal systemd notify (READY/WATCHDOG, no libsystemd)
│   ├── diagnostics.h            # 🔹 runtime diagnostics (main loop timing)
│   ├── totp.h                   # 🔹 TOTP 2FA (RFC 6238, HMAC-SHA1 via OpenSSL)
│   ├── telegram.h               # 🔹 Telegram API client interface (public)
│   ├── telegram_http.h          # 🔹 low-level HTTP communication
│   ├── telegram_parser.h        # 🔹 JSON parsing and message formatting
│   ├── telegram_poll.h          # 🔹 long polling with fork isolation
│   ├── telegram_offset.h        # 🔹 update offset persistence
│   ├── telegram_timeouts.h      # 🔹 timeout constants with _Static_assert chain
│   ├── commands.h               # 🔹 command dispatcher, requires_confirmation, pending_confirm_t, commands_request_confirm()
│   ├── reply.h                  # 🔹 unified response formatting API
│   ├── security.h               # 🔹 security layer (access control, tokens, rate limiting)
│   ├── system.h                 # 🔹 system info (metrics, uptime, host info)
│   ├── services.h               # 🔹 systemd services status and action API
│   ├── services_config.h        # 🔹 shared service definitions (alias, unit, display)
│   ├── users.h                  # 🔹 active user sessions API
│   ├── logs.h                   # 🔹 logs retrieval API (journalctl integration)
│   ├── logs_filter.h            # 🔹 log filtering API (semantic + multi-keyword)
│   ├── logstat.h                # 🔹 log statistics analyzer (SSE4.2 accelerated)
│   ├── utils.h                  # 🔹 shared helpers (strings, parsing, formatting)
│   ├── cmd_help.h               # 🔹 /help command handler API
│   ├── cmd_system.h             # 🔹 /start, /status, /health, /ping, /about, /logstat API
│   ├── cmd_services.h           # 🔹 /services, /users, /logs, /service API
│   ├── cmd_security.h           # 🔹 /fail2ban command handler API
│   └── cmd_control.h            # 🔹 /reboot, /restart, /totp_setup API
│
├── src/                         # 📂 implementation (core modules)
│   ├── main.c                   # 🚀 entry point (init, orchestration, main loop)
│   ├── cli.c                    # 🔹 command-line argument parsing
│   ├── config.c                 # 🔹 config parser, reload, TOTP_SECRET validation
│   ├── logger.c                 # 🔹 thread-safe logging, early buffer, isatty mirror
│   ├── lifecycle.c              # 🔹 signal handlers, graceful shutdown, reboot/restart
│   ├── environment.c            # 🔹 startup diagnostics and access checks
│   ├── exec.c                   # 🔹 external command execution with timeout
│   ├── metrics.c                # 🔹 bot metrics collection and formatting
│   ├── diagnostics.c            # 🔹 main loop iteration timing and diagnostics
│   ├── totp.c                   # 🔹 TOTP implementation (base32, HMAC-SHA1, RFC 6238)
│   ├── telegram.c               # 🔹 public API (init, send, polling)
│   ├── telegram_http.c          # 🔹 curl-based HTTP requests
│   ├── telegram_parser.c        # 🔹 JSON parsing + markdown escaping
│   ├── telegram_poll.c          # 🔹 long polling with fork() isolation
│   ├── telegram_offset.c        # 🔹 offset persistence (crash recovery)
│   ├── commands.c               # 🔹 command routing, two-step confirmation, /confirm
│   ├── reply.c                  # 🔹 response formatting helpers
│   ├── security.c               # 🔹 access control, rate limiting, token validation
│   ├── system.c                 # 🔹 system metrics and information
│   ├── services.c               # 🔹 systemd service status queries and actions
│   ├── services_config.c        # 🔹 service table (single source of truth)
│   ├── users.c                  # 🔹 active user session enumeration
│   ├── logs.c                   # 🔹 journalctl log retrieval and formatting
│   ├── logs_filter.c            # 🔹 semantic log filtering engine
│   ├── logstat.c                # 🔹 SSE4.2 log file statistics analyzer
│   ├── utils.c                  # 🔹 helper functions implementation
│   │
│   └── commands/                # 📂 command handlers (V2)
│       ├── cmd_help.c           # 🧩 /help
│       ├── cmd_system.c         # 🧩 /start, /status, /health, /ping, /about, /logstat
│       ├── cmd_services.c       # 🧩 /services, /users, /logs, /service
│       ├── cmd_security.c       # 🧩 /fail2ban
│       └── cmd_control.c        # 🧩 /reboot, /restart, /totp_setup
│
├── tools/                       # 🔧 internal utilities
│   └── f2b-wrapper.c            # 🔹 Fail2Ban wrapper (ban/unban/status)
│
└── build/                       # 🏗 build artifacts (created by Makefile)
    └── *.o                      # compiled object files
```

## Module Organization

| Module | Header | Source | Responsibility |
|--------|--------|--------|----------------|
| **CLI** | `cli.h` | `cli.c` | Command-line argument parsing, help/version output |
| **Config** | `config.h` | `config.c` | Configuration file loading, validation, reload, TOTP secret |
| **Logger** | `logger.h` | `logger.c` | Thread-safe logging, early buffer, isatty mirror to stderr |
| **Lifecycle** | `lifecycle.h` | `lifecycle.c` | Signal handlers, graceful shutdown, reboot/restart |
| **Environment** | `environment.h` | `environment.c` | Startup diagnostics, access checks, CI detection |
| **Exec** | `exec.h` | `exec.c` | External command execution with deadline timeout |
| **Diagnostics** | `diagnostics.h` | `diagnostics.c` | Main loop iteration timing, slow iteration warnings |
| **TOTP** | `totp.h` | `totp.c` | RFC 6238 TOTP (base32, HMAC-SHA1, verify, URI generation) |
| **Telegram** | `telegram.h` | `telegram.c` | Public API (init, shutdown, send) |
| **Telegram HTTP** | `telegram_http.h` | `telegram_http.c` | Low-level HTTP requests via libcurl |
| **Telegram Parser** | `telegram_parser.h` | `telegram_parser.c` | JSON parsing, markdown escaping, message truncation |
| **Telegram Poll** | `telegram_poll.h` | `telegram_poll.c` | Long polling with fork() isolation and shutdown check |
| **Telegram Offset** | `telegram_offset.h` | `telegram_offset.c` | Update offset persistence (crash recovery) |
| **Commands** | `commands.h` | `commands.c` | Command routing, two-step confirmation, /confirm dispatcher |
| **Reply** | `reply.h` | `reply.c` | Unified response formatting for handlers |
| **Security** | `security.h` | `security.c` | Access control, input validation, tokens, rate limiting |
| **System** | `system.h` | `system.c` | System metrics, uptime, OS/hardware info |
| **Services** | `services.h` | `services.c` | Systemd service status queries and start/stop/restart |
| **Services Config** | `services_config.h` | `services_config.c` | Service definitions (single source of truth) |
| **Users** | `users.h` | `users.c` | Active user session enumeration via utmp |
| **Logs** | `logs.h` | `logs.c` | Journalctl log retrieval and formatting |
| **Logs Filter** | `logs_filter.h` | `logs_filter.c` | Semantic and multi-keyword log filtering |
| **Logstat** | `logstat.h` | `logstat.c` | SSE4.2 log file statistics analyzer |
| **Utils** | `utils.h` | `utils.c` | String manipulation, parsing, time measurement |
| **Metrics** | `metrics.h` | `metrics.c` | Bot usage statistics collection and formatting |

## Command Handlers

| Command | Handler | Source | Confirmation |
|---------|---------|--------|--------------|
| `/start` | `cmd_start_v2` | `cmd_system.c` | — |
| `/help` | `cmd_help_v2` | `cmd_help.c` | — |
| `/status` | `cmd_status_v2` | `cmd_system.c` | — |
| `/health` | `cmd_health_v2` | `cmd_system.c` | — |
| `/about` | `cmd_about_v2` | `cmd_system.c` | — |
| `/ping` | `cmd_ping_v2` | `cmd_system.c` | — |
| `/logstat` | `cmd_logstat_v2` | `cmd_system.c` | — |
| `/services` | `cmd_services_v2` | `cmd_services.c` | — |
| `/service` | `cmd_service_v2` | `cmd_services.c` | 🔐 start/stop/restart only |
| `/users` | `cmd_users_v2` | `cmd_services.c` | — |
| `/logs` | `cmd_logs_v2` | `cmd_services.c` | — |
| `/fail2ban` | `cmd_fail2ban_v2` | `cmd_security.c` | — |
| `/reboot` | `cmd_reboot_v2` | `cmd_control.c` | 🔐 required |
| `/restart` | `cmd_restart_v2` | `cmd_control.c` | 🔐 required |
| `/totp_setup` | `cmd_totp_setup_v2` | `cmd_control.c` | — |
| `/confirm` | inline in dispatcher | `commands.c` | — |

---

## Command Architecture

### V2 Handler Model

All commands use the V2 handler signature:

```c
int handler_v2(command_ctx_t *ctx);
```

**command_ctx_t:**
```c
typedef struct {
    long           chat_id;     /* Telegram chat ID                        */
    int            user_id;     /* Telegram user ID                        */
    const char    *username;    /* Telegram username (may be NULL)         */
    const char    *args;        /* Arguments after command name            */
    const char    *raw_text;    /* Full command text                       */
    time_t         msg_date;    /* Message timestamp (for /ping latency)   */
    char          *response;    /* Response buffer (pre-allocated)         */
    size_t         resp_size;   /* Size of response buffer                 */
    response_type_t *resp_type; /* Output: RESP_MARKDOWN or RESP_PLAIN     */
    unsigned short req_id;      /* 16-bit request ID for log correlation   */
} command_ctx_t;
```

### Two-Step Confirmation

Two confirmation patterns are supported:

**Pattern A — `requires_confirmation = 1` (dispatcher-initiated):**
Used by `/reboot` and `/restart`. The dispatcher intercepts the command
before the handler is called.

```
user sends /reboot
    ↓
dispatcher detects requires_confirmation = 1
    ↓
stores pending_confirm_t { command="/reboot", args="", expires_at=now+TTL }
    ↓
if TOTP_SECRET set → asks for authenticator code
if TOTP_SECRET empty → generates classic token, sends it
    ↓
user sends /confirm <code>
    ↓
dispatcher validates code (totp_verify or security_validate_reboot_token)
    ↓
on success → restores ctx->args from pending, calls cmd_reboot_v2()
on failure → clears pending slot, returns error
```

**Pattern B — `requires_confirmation = 0` + `commands_request_confirm()` (handler-initiated):**
Used by `/service` — only `start`/`stop`/`restart` need confirmation, not `status`.
The handler calls `commands_request_confirm()` directly for those actions.

```
user sends /service ssh restart
    ↓
dispatcher calls cmd_service_v2() directly (no gate)
    ↓
handler parses alias="ssh" action="restart"
    ↓
handler validates alias exists, then calls commands_request_confirm(ctx, "/service", "ssh restart")
    ↓
pending_confirm_t saved: { command="/service", args="ssh restart", expires_at=now+TTL }
    ↓
user sends /confirm <code>
    ↓
dispatcher validates code, restores ctx->args = "ssh restart"
    ↓
calls cmd_service_v2() again → re-parses args → executes service_action()
```

**pending_confirm_t:**
```c
typedef struct {
    long   chat_id;     /* Who initiated the command          */
    char   command[32]; /* Command name e.g. "/reboot"        */
    char   args[64];    /* Original arguments e.g. "ssh restart" */
    int    token;       /* Expected token (classic flow only) */
    time_t expires_at;  /* When this pending entry expires    */
    int    active;      /* 1 = waiting for confirmation       */
} pending_confirm_t;
```

**Confirmation modes:**

| Mode | Condition | Validation |
|------|-----------|------------|
| TOTP | `TOTP_SECRET` set in config | `totp_verify()` — RFC 6238 |
| Token | `TOTP_SECRET` empty | `security_validate_reboot_token()` — stateless |

Both modes use the same `/confirm <code>` command — the dispatcher selects
the validation method automatically based on config.

### Dispatcher (commands.c)

Responsibilities in order:
1. Input validation (`security_validate_text`)
2. Rate limiting (`security_rate_limit`)
3. Access control (`security_check_access`)
4. `/confirm` — handled inline before main loop
5. Command lookup in table
6. `requires_confirmation` gate — request confirmation if needed
7. Handler execution (V2)

### Migration Status

| Module | Status | Commands |
|--------|--------|----------|
| Services | ✅ V2 | /services, /users, /logs, /service |
| System | ✅ V2 | /start, /status, /health, /about, /ping, /logstat |
| Help | ✅ V2 | /help |
| Security | ✅ V2 | /fail2ban |
| Control | ✅ V2 | /reboot, /restart, /totp_setup |

**All 16 commands use V2. Legacy code completely removed.**

---

## TOTP Module (totp.c)

Implements RFC 6238 (TOTP) over RFC 4226 (HOTP) using HMAC-SHA1
via OpenSSL (already statically linked).

**Algorithm:**
```
counter  = floor(unix_time / 30)
msg      = counter as 8-byte big-endian uint64
hmac     = HMAC-SHA1(base32_decode(secret), msg)
offset   = hmac[19] & 0x0f
truncated= (hmac[offset..offset+3] & 0x7fffffff)
code     = truncated % 1000000
```

**Verification window:** ±1 step (±30 seconds) to compensate for clock skew.

**Public API:**
- `totp_generate(secret, t, &code)` — generate code for timestamp
- `totp_verify(secret, input_code)` — verify with window
- `totp_validate_secret(secret)` — validate base32 format
- `totp_uri(secret, label, issuer, buf, size)` — generate otpauth:// URI
- `base32_decode(encoded, decoded, size)` — RFC 4648 decoder

---

## Logger Architecture

**Early buffer:** Messages logged before the log file opens are stored
in memory (32 × 512 B) and flushed to the file on first successful
`logger_init()`. Nothing is lost at startup.

**isatty mirror:** When stderr is a terminal (interactive run),
logs are mirrored to stderr in addition to the file. When running
as a systemd service (stderr redirected), only the file is written —
no duplicate entries.

**Timestamp:** Single `clock_gettime(CLOCK_REALTIME)` call per message —
no race between `time()` and `clock_gettime()` that could produce
mismatched seconds and milliseconds.

**Log format:**
```
[2026-05-06 14:32:11.123] [ INFO ] [ NET ] message text
```
Fields are center-aligned to fixed widths:
- Level: 6 chars (`[ INFO ]`, `[ERROR ]`, `[ WARN ]`, `[DEBUG ]`)
- Tag: 5 chars (`[ NET ]`, `[ SYS ]`, `[STATE]`, `[ CFG ]`, etc.)

**Fallback:** If the log file cannot be opened, all output goes to stderr.

---

## Execution Layer

`exec.c` provides fork/exec abstraction with:
- `pipe2(O_CLOEXEC)` — no fd leaks into child processes
- Deadline-based `select()` — total timeout is always exactly `timeout_ms`
- Blocking `waitpid()` after pipe EOF — zero zombie window
- SIGTERM → 100ms grace → SIGKILL escalation in `kill_and_reap()`

---

## Telegram Poll Architecture

`telegram_poll.c` runs each HTTP request in a child process to isolate
libcurl from the main event loop:

```
parent                        child
  |-- fork() ----------------> |
  |                            |-- telegram_http_request()
  |-- poll(pipe, POLLIN) <-----|-- write(pipe, result)
  |                            |-- _exit(0)
  |-- read(pipe) --------------|
  |-- waitpid(blocking) -------|  (child already dead)
```

- **Dynamic pipe buffer:** `read_pipe_data()` allocates `malloc(data_len+1)`
  so there is no fixed cap — handles 100+ accumulated updates without truncation.
- **RSS sampling:** Combined RSS of parent + child is sampled immediately
  after `fork()` while both processes are alive. Result is read by `/start`
  selfcheck via `g_poll_rss_kb` / `g_poll_rss_procs`.
- **Child signal detection:** `waitpid()` checks `WIFSIGNALED` — unexpected
  child crashes (OOM, SIGSEGV) are logged at WARN level.

---

## Log Statistics (logstat.c)

`logstat.c` analyzes the bot log file using SSE4.2-accelerated processing.
Compiled with `-msse4.2` flag (separate Makefile rule).

### SSE4.2 newline counting

Processes 16 bytes per iteration using `PCMPISTRM` instruction instead
of a byte-by-byte loop:

```
needle: xmm0 = [0A 0A 0A 0A 0A 0A 0A 0A 0A 0A 0A 0A 0A 0A 0A 0A]  (16x '\n')
input:  xmm1 = [M  a  y  _  0  6  _  1  4  :  3  2  \n X  X  X ]

PCMPISTRM $0x00, xmm1, xmm0   →  mask = 0b0001000000000000
PMOVMSKB  xmm0, eax           →  eax  = 4096
POPCNT    eax,  eax           →  eax  = 1  (one newline found)
```

On a 1MB log file: ~65,000 SSE4.2 iterations vs ~1,000,000 scalar comparisons.

### Scalar pass

After newline counting, a scalar pass extracts metadata from each line
using fixed offsets (format is guaranteed by our logger):

```
[2026-05-06 14:32:11.123] [ INFO ] [ NET ] message
 ^offset 0                ^26      ^35
 timestamp                level    tag
```

- **Hour** — extracted from offset 12-13 for per-hour histogram
- **Level** — `strncmp` at offset 26 against `[ERROR ]`, `[ WARN ]`, etc.
- **Tag** — `strncmp` at offset 35-36 against `NET`, `SYS`, `STATE`, etc.

### Output (`/logstat`)

```
📊 Log Statistics

File size : 1.2 MB
Lines     : 8,421

Levels
  ERROR   : 2
  WARN    : 14
  INFO    : 7,893
  DEBUG   : 512

Tags
  NET     : 4,102 (48%)
  SYS     : 1,205 (14%)
  STATE   : 987   (11%)
  CMD     : 876   (10%)
  CFG     : 541   (6%)

Busiest   : 14:00-15:00 (342 lines)
```

---

## Timeout Chain

All Telegram communication timeouts are centralized in `telegram_timeouts.h`
with compile-time invariant checks:

```
CONNECT (5s) < POLL (25s) < HTTP (35s) < CHILD_WAIT (38000ms)
```

`_Static_assert` enforces this chain at compile time — wrong values
cause a build error with a descriptive message.

---

## Security Model

- **Access control:** single `chat_id` validated on every request
- **Input validation:** control character filtering at dispatcher level
- **Rate limiting:** max 5 commands/second, burst capped at 16
- **Two-step confirmation:** TOTP or stateless token for dangerous commands
- **Bruteforce protection:** 30-second block after 5 failed attempts
- **Replay protection:** token invalidated on first successful use
- **No shell injection:** IP validation via `inet_pton`, service names via whitelist
- **No direct root:** all privileged operations via sudo whitelist
- **Unpredictable salt:** `getrandom(2)` at startup, never logged
- **Secret masking:** TOKEN and TOTP_SECRET never appear in log files

---

## Data Flow

```
Telegram update received
    ↓
telegram_poll.c — fork() child, HTTP request via libcurl
    ↓
telegram_parser.c — JSON parsing, extract message
    ↓
commands.c — validate input, rate limit, access control
    ↓
/confirm? → handle_confirm() → TOTP or token validation → restore args
    ↓
requires_confirmation? → request_confirmation() → store pending slot
    ↓
handler_v2(ctx) — command implementation
    ↓
exec.c (if needed) — fork/exec with timeout
    ↓
reply_markdown() / reply_plain() — format response
    ↓
telegram_http.c — sendMessage API call
```

---

## Design Principles

- **Minimalism** — no heavy frameworks, pure C, ~5.5MB static binary
- **Security-first** — validate everything, least privilege, audit log
- **Predictability** — deadline timeouts, no busy-wait, no zombie window
- **Modularity** — single responsibility per module, clean public APIs
- **Operational maturity** — systemd integration, logrotate, CI/CD, build numbers
- **Backward compatibility** — TOTP is optional, classic token flow is fallback

---

## Future Improvements

- Alerts — bot notifies on service failure or high resource usage
- Log filtering by time (`--since 1h`)
- `/df` — disk usage quick view
- `/top` — top processes by CPU/RAM
- `/ssh keys` — show authorized SSH keys
- Unit tests — TOTP RFC 6238 vectors, security.c, logs_filter.c
- Prometheus metrics export (requires >2GB RAM)
