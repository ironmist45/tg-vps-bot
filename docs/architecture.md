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
│       ├── build-static.yml          # 🔧 CI/CD pipeline GCC 7.5 (legacy, fallback)
│       ├── build-static-gcc9.yml     # 🔧 CI/CD pipeline GCC 9.4.0 (primary)
│       └── build-static-gcc14.yml    # 🔧 CI/CD pipeline GCC 14
│
├── config/
│   └── config.example.conf      # 🔧 example configuration (env template)
│
├── include/                     # 📂 public headers (module APIs)
│   ├── version.h                # 🔹 version and build information
│   ├── build_info.h             # 🔹 generated build info (commit, date, build number, compiler)
│   ├── cli.h                    # 🔹 command-line interface parsing (-c, -p, -h, -v)
│   ├── config.h                 # 🔹 config loader API (parsing, reload, TOTP secret, upload, SSH keys)
│   ├── logger.h                 # 🔹 logging system API (levels, macros, early buffer, mirror control)
│   ├── lifecycle.h              # 🔹 process lifecycle (signals, shutdown, reboot)
│   ├── environment.h            # 🔹 runtime environment diagnostics
│   ├── exec.h                   # 🔹 execution API (command runner with timeout)
│   ├── metrics.h                # 🔹 bot usage metrics and statistics
│   ├── sd_notify.h              # 🔹 minimal systemd notify (READY/WATCHDOG, no libsystemd)
│   ├── diagnostics.h            # 🔹 runtime diagnostics (main loop timing)
│   ├── totp.h                   # 🔹 TOTP 2FA (RFC 6238, HMAC-SHA1 via OpenSSL)
│   ├── telegram.h               # 🔹 Telegram API client interface (public)
│   ├── telegram_http.h          # 🔹 low-level HTTP communication, file download streaming
│   ├── telegram_parser.h        # 🔹 JSON parsing, message formatting, document updates
│   ├── telegram_poll.h          # 🔹 long polling with fork isolation
│   ├── telegram_offset.h        # 🔹 update offset persistence
│   ├── telegram_timeouts.h      # 🔹 timeout constants with _Static_assert chain
│   ├── tg_paths.h               # 🔹 compile-time filesystem path constants (data dir, offset file)
│   ├── commands.h               # 🔹 command dispatcher, requires_confirmation, pending_confirm_t, commands_request_confirm()
│   ├── reply.h                  # 🔹 unified response formatting API
│   ├── security.h               # 🔹 security layer (access control, tokens, rate limiting)
│   ├── system.h                 # 🔹 system info (metrics, uptime, hostname, host info)
│   ├── services.h               # 🔹 systemd services status and action API
│   ├── services_config.h        # 🔹 shared service definitions (alias, unit, display)
│   ├── upload.h                 # 🔹 file upload API (getFile, download, save, list)
│   ├── users.h                  # 🔹 active user sessions API
│   ├── logs.h                   # 🔹 logs retrieval API (journalctl integration)
│   ├── logs_filter.h            # 🔹 log filtering API (semantic + multi-keyword)
│   ├── logstat.h                # 🔹 log statistics analyzer (SSE4.2 accelerated)
│   ├── utils.h                  # 🔹 shared helpers (strings, parsing, formatting, RESP_MAX)
│   ├── cmd_help.h               # 🔹 /help command handler API
│   ├── cmd_system.h             # 🔹 /start, /status, /health, /ping, /about, /logstat API
│   ├── cmd_services.h           # 🔹 /services, /users, /logs, /service API
│   ├── cmd_security.h           # 🔹 /fail2ban, /sshkeys command handlers API
│   ├── cmd_control.h            # 🔹 /reboot, /restart, /totp_setup API
│   └── cmd_upload.h             # 🔹 file upload handlers API (/files, incoming files)
│
├── src/                         # 📂 implementation (core modules)
│   ├── main.c                   # 🚀 entry point (init, orchestration, main loop)
│   ├── cli.c                    # 🔹 command-line argument parsing (-c, -p/--parse, -h, -v)
│   ├── config.c                 # 🔹 config parser, reload, TOTP_SECRET, UPLOAD_ENABLED, SSH_KEYS_PATH
│   ├── logger.c                 # 🔹 thread-safe logging, early buffer, isatty mirror, set_mirror()
│   ├── lifecycle.c              # 🔹 signal handlers, graceful shutdown, reboot via sudo
│   ├── environment.c            # 🔹 startup diagnostics and access checks
│   ├── exec.c                   # 🔹 external command execution with timeout
│   ├── metrics.c                # 🔹 bot metrics collection and formatting
│   ├── diagnostics.c            # 🔹 main loop iteration timing and diagnostics
│   ├── totp.c                   # 🔹 TOTP implementation (base32, HMAC-SHA1, RFC 6238)
│   ├── telegram.c               # 🔹 public API (init, send, polling); callers responsible for MarkdownV2 escaping
│   ├── telegram_http.c          # 🔹 curl-based HTTP requests, file download streaming, structured API error logging
│   ├── telegram_parser.c        # 🔹 JSON parsing + markdown escaping + document parsing
│   ├── telegram_poll.c          # 🔹 long polling with fork() isolation, file routing
│   ├── telegram_offset.c        # 🔹 offset persistence with fsync (crash recovery), uses tg_paths.h
│   ├── commands.c               # 🔹 command routing, two-step confirmation, /confirm, slot overwrite warning
│   ├── reply.c                  # 🔹 response formatting helpers (RESP_MAX throughout)
│   ├── security.c               # 🔹 access control, rate limiting, token validation
│   ├── system.c                 # 🔹 system metrics, hostname, OS/hardware info
│   ├── services.c               # 🔹 systemd service status queries and actions
│   ├── services_config.c        # 🔹 service table (single source of truth)
│   ├── upload.c                 # 🔹 Telegram file receiving (getFile API, disk streaming)
│   ├── users.c                  # 🔹 active user session enumeration via utmp
│   ├── logs.c                   # 🔹 journalctl log retrieval and formatting
│   ├── logs_filter.c            # 🔹 semantic log filtering engine
│   ├── logstat.c                # 🔹 SSE4.2 log file statistics analyzer
│   ├── utils.c                  # 🔹 helper functions implementation
│   │
│   └── commands/                # 📂 command handlers (V2)
│       ├── cmd_help.c           # 🧩 /help
│       ├── cmd_system.c         # 🧩 /start, /status, /health, /ping, /about, /logstat
│       ├── cmd_services.c       # 🧩 /services, /users, /logs, /service
│       ├── cmd_security.c       # 🧩 /fail2ban, /sshkeys
│       ├── cmd_control.c        # 🧩 /reboot, /restart, /totp_setup
│       └── cmd_upload.c         # 🧩 incoming files, /files
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
| **CLI** | `cli.h` | `cli.c` | Command-line argument parsing, help/version output, --parse config validation |
| **Config** | `config.h` | `config.c` | Configuration file loading, validation, reload, TOTP secret, upload flag, SSH keys path |
| **Logger** | `logger.h` | `logger.c` | Thread-safe logging, early buffer, isatty mirror to stderr, mirror override |
| **Lifecycle** | `lifecycle.h` | `lifecycle.c` | Signal handlers, graceful shutdown, reboot via sudo /sbin/reboot |
| **Environment** | `environment.h` | `environment.c` | Startup diagnostics, access checks, CI detection |
| **Exec** | `exec.h` | `exec.c` | External command execution with deadline timeout |
| **Diagnostics** | `diagnostics.h` | `diagnostics.c` | Main loop iteration timing, slow iteration warnings |
| **TOTP** | `totp.h` | `totp.c` | RFC 6238 TOTP (base32, HMAC-SHA1, verify, URI generation) |
| **Telegram** | `telegram.h` | `telegram.c` | Public API (init, shutdown, send); callers own MarkdownV2 escaping |
| **Telegram HTTP** | `telegram_http.h` | `telegram_http.c` | Low-level HTTP requests via libcurl, file download streaming, structured API error logging |
| **Telegram Parser** | `telegram_parser.h` | `telegram_parser.c` | JSON parsing, markdown escaping, document update parsing |
| **Telegram Poll** | `telegram_poll.h` | `telegram_poll.c` | Long polling with fork() isolation, file/text routing |
| **Telegram Offset** | `telegram_offset.h` | `telegram_offset.c` | Update offset persistence with fsync (crash recovery) |
| **Paths** | `tg_paths.h` | — | Compile-time filesystem constants (TG_DATA_DIR, TG_OFFSET_FILE, TG_OFFSET_TMP) |
| **Commands** | `commands.h` | `commands.c` | Command routing, two-step confirmation, /confirm dispatcher, slot overwrite warning |
| **Reply** | `reply.h` | `reply.c` | Unified response formatting for handlers (RESP_MAX = 8192) |
| **Security** | `security.h` | `security.c` | Access control, input validation, tokens, rate limiting |
| **System** | `system.h` | `system.c` | System metrics, uptime, hostname, OS/hardware info |
| **Services** | `services.h` | `services.c` | Systemd service status queries and start/stop/restart |
| **Services Config** | `services_config.h` | `services_config.c` | Service definitions (single source of truth) |
| **Upload** | `upload.h` | `upload.c` | Telegram file receiving, getFile API, disk streaming, file listing |
| **Users** | `users.h` | `users.c` | Active user session enumeration via utmp |
| **Logs** | `logs.h` | `logs.c` | Journalctl log retrieval and formatting |
| **Logs Filter** | `logs_filter.h` | `logs_filter.c` | Semantic and multi-keyword log filtering |
| **Logstat** | `logstat.h` | `logstat.c` | SSE4.2 log file statistics analyzer |
| **Utils** | `utils.h` | `utils.c` | String manipulation, parsing, time measurement, RESP_MAX |
| **Metrics** | `metrics.h` | `metrics.c` | Bot usage statistics collection and formatting |

## Command Handlers

| Command | Handler | Source | Category | Confirmation |
|---------|---------|--------|----------|--------------|
| `/start` | `cmd_start_v2` | `cmd_system.c` | General | — |
| `/help` | `cmd_help_v2` | `cmd_help.c` | General | — |
| `/status` | `cmd_status_v2` | `cmd_system.c` | Server | — |
| `/health` | `cmd_health_v2` | `cmd_system.c` | Bot | — |
| `/about` | `cmd_about_v2` | `cmd_system.c` | Bot | — |
| `/ping` | `cmd_ping_v2` | `cmd_system.c` | Bot | — |
| `/logstat` | `cmd_logstat_v2` | `cmd_system.c` | Bot | — |
| `/services` | `cmd_services_v2` | `cmd_services.c` | Services | — |
| `/service` | `cmd_service_v2` | `cmd_services.c` | Services | 🔐 start/stop/restart only |
| `/users` | `cmd_users_v2` | `cmd_services.c` | Services | — |
| `/logs` | `cmd_logs_v2` | `cmd_services.c` | Services | — |
| `/files` | `cmd_files_v2` | `cmd_upload.c` | Services | — |
| `/fail2ban` | `cmd_fail2ban_v2` | `cmd_security.c` | Security | — |
| `/sshkeys` | `cmd_sshkeys_v2` | `cmd_security.c` | Security | — |
| `/reboot` | `cmd_reboot_v2` | `cmd_control.c` | System control | 🔐 required |
| `/restart` | `cmd_restart_v2` | `cmd_control.c` | System control | 🔐 required |
| `/totp_setup` | `cmd_totp_setup_v2` | `cmd_control.c` | System control | — |
| `/confirm` | inline in dispatcher | `commands.c` | — | — |
| *(incoming file)* | `cmd_handle_upload` | `cmd_upload.c` | — | — |

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

**Slot overwrite behaviour:**
If a new confirmable command arrives while a *different* command is pending
(e.g. `/restart` while `/reboot` is awaiting confirmation), the old slot is
overwritten and the user receives an inline cancellation notice prepended to
the new confirmation request:

```
⚠️ `/reboot` cancelled.

🔐 Confirm: `/restart`
...
```

This prevents silent loss of a pending confirmation and applies to both
dispatcher-initiated and handler-initiated (commands_request_confirm) paths.

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
| Security | ✅ V2 | /fail2ban, /sshkeys |
| Control | ✅ V2 | /reboot, /restart, /totp_setup |
| Upload | ✅ V2 | /files, incoming files |

**All 18 commands use V2. Legacy code completely removed.**

---

## CLI Mode — --parse

`tg-bot -p <path>` / `tg-bot --parse <path>` validates a config file
without starting the bot or connecting to Telegram.

```
tg-bot --parse /etc/tg-bot/config.conf
    ↓
cli_cmd_parse_config()
    ↓
logger redirected to /dev/null + mirror disabled   (no CFG log output)
    ↓
config_load() — full parse, same as normal startup
    ↓
logger_close()
    ↓
print sections with ANSI color indicators:
  green ✓  — value present and valid
  red   ✗  — missing or invalid (counts as error)
  yellow ⚠ — optional or not accessible by current user (counts as warning)
    ↓
filesystem checks:
  LOG_FILE parent dir  — W_OK, warn_only (owned by tg-bot)
  UPLOAD_DIR           — W_OK, warn_only (owned by tg-bot)
  SSH_KEYS_PATH        — R_OK, warn_only (read via sudo cat at runtime)
  SUDO_PATH            — X_OK, error if missing
  SYSTEMCTL_PATH       — X_OK, error if missing
  JOURNALCTL_PATH      — X_OK, error if missing
  F2B_WRAPPER_PATH     — X_OK, error if missing
    ↓
exit 0 (Config OK) or exit 1 (Config FAILED)
```

**warn_only** paths are owned by `tg-bot` or accessed via sudo at runtime —
not writable/readable by the invoking user. Missing paths are always errors
regardless of `warn_only`.

Useful for:
- Manual config verification before deploying
- `ExecStartPre=` in systemd unit — bot won't start if config is broken
- CI/CD validation pipelines

---

## /sshkeys Command

Reads `SSH_KEYS_PATH` (configured in `config.conf`) via `sudo cat` and
displays key type and comment for each entry. The key blob itself is
not shown — it is long and provides no useful information.

```
/sshkeys
    ↓
cmd_sshkeys_v2()
    ↓
SSH_KEYS_PATH empty? → reply: configuration hint + instructions
    ↓
exec_command([sudo, -n, /bin/cat, path])
    ↓
sshkeys_parse_line() per line:
  skip empty lines and # comments
  skip optional options field (handles quoted values with spaces)
  identify keytype token via s_key_types[] lookup
  strip ssh-/ecdsa-sha2- prefix for display (pretty_type)
  skip base64 blob
  remainder = comment (or "(no comment)")
    ↓
reply_plain(): numbered list of "type — comment" + total count
```

**Supported key types:** `ssh-rsa`, `ssh-dss`, `ssh-ed25519`,
`ecdsa-sha2-nistp{256,384,521}`, `sk-ssh-ed25519@openssh.com`,
`sk-ecdsa-sha2-nistp256@openssh.com`

**Config:** `SSH_KEYS_PATH=/home/user/.ssh/authorized_keys` (default: empty = disabled)

**Sudoers:** `tg-bot ALL=(ALL) NOPASSWD: /bin/cat /home/user/.ssh/authorized_keys`

---

## Paths Architecture

Internal filesystem paths are defined as compile-time constants in `tg_paths.h`
(named `tg_paths.h` to avoid shadowing the system `<paths.h>` header which
defines `_PATH_UTMP` used by `users.c`).

```c
#define TG_DATA_DIR    "/var/lib/tg-bot"
#define TG_OFFSET_FILE TG_DATA_DIR "/offset.dat"
#define TG_OFFSET_TMP  TG_DATA_DIR "/offset.tmp"
```

**Principle:** internal state paths (offset file, future state) belong in
`tg_paths.h`; user-configurable paths (LOG_FILE, UPLOAD_DIR, SSH_KEYS_PATH)
belong in the config file. Changing `TG_DATA_DIR` updates all derived paths
automatically at compile time.

---

## File Upload Architecture

File upload is disabled by default (`UPLOAD_ENABLED=no`). When enabled,
incoming Telegram document updates are routed through the upload pipeline:

```
User sends file via Telegram
    ↓
telegram_parser.c — parses document object, extracts file_id/file_name/file_size
    ↓
telegram_poll.c — detects u->file_id != NULL
    ↓
security_is_allowed_chat() — access check (same as text commands)
    ↓
UPLOAD_ENABLED check — if disabled: log INFO + send "⚠️ File upload is disabled"
    ↓
cmd_handle_upload() — packs file_id, file_name, file_size into ctx->args
                       as "file_id\nfile_name\nfile_size"
    ↓
if file_size > 1 MB → telegram_send_plain("📥 Uploading...")
    ↓
upload_receive_file() — calls getFile API → gets file_path on Telegram servers
    ↓
telegram_http_download_file() — streams file directly to disk via write_file_callback
    ↓
Saves to g_cfg.upload_dir/<sanitized_filename>
    ↓
Reply: "Saved: config.conf (1.2 KB)" (plain text)
```

**Filename sanitization:**
- Allowed characters: `[a-zA-Z0-9._-]`
- Spaces replaced with `_`
- Names starting with `.` are rejected (covers hidden files and `..`)
- Path traversal impossible (no slashes)

**Telegram limit:** 20 MB per file.

**Config keys:**
- `UPLOAD_ENABLED=yes` — enable upload (default: disabled)
- `UPLOAD_DIR=/var/www/html/uploads` — destination directory

---

## Response Buffer Architecture

All command handlers write responses via `reply_plain()` / `reply_markdown()`
from `reply.c`. The buffer chain is:

```
telegram_poll.c: char response[BOT_RESP_MAX=8192]
    ↓ passed as ctx->response / ctx->resp_size
reply_write(): snprintf(ctx->response, ctx->resp_size, ...)
    ↓ on overflow: null-terminate, log WARN, graceful degradation
telegram_send_message()
    ↓
telegram_truncate_message(): hard cap at TG_LIMIT=4096 (Telegram API limit)
```

**Constants:**
- `RESP_MAX = 8192` (utils.h) — handler internal buffers (`char tmp[RESP_MAX]`)
- `BOT_RESP_MAX = 8192` (telegram_poll.c) — response buffer allocated per request
- `TG_LIMIT = 4096` (telegram_parser.c) — Telegram message length limit

Handlers use `RESP_MAX` for consistency. `telegram_truncate_message()` ensures
nothing exceeds the Telegram API limit regardless of handler output size.

---

## MarkdownV2 Formatting Model

`telegram_send_message()` sends text as-is with `parse_mode=MarkdownV2`.
It does **not** escape the text — callers are responsible for correct formatting.

**Rules for handlers:**
- Use `reply_plain()` when response contains dynamic data with special chars
  (dots in numbers, hyphens in names, etc.)
- Use `reply_markdown()` only when MarkdownV2 formatting is intentional
- Wrap dynamic data in backtick inline code (`` `value` ``) — dots and hyphens
  are safe inside code spans
- Use ` ``` ` code blocks for multi-line system output — no escaping needed inside
- Escape user-supplied strings via `telegram_escape_markdown()` before inserting
  into Markdown responses
- Special chars that must be escaped outside code spans:
  `_ [ ] ( ) ~ > # + = | { } . ! \` and in some contexts `-`

**API error logging:** `telegram_http_request()` logs Telegram error responses
at WARN level when `{"ok":false}` is returned — parses `error_code` and
`description` via `strstr/sscanf`, no cJSON dependency in the HTTP layer.
Format: `sendMessage API error 401: Unauthorized`

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
in memory (32 × 1088 B) and flushed to the file on first successful
`logger_init()`. Nothing is lost at startup.

**isatty mirror:** When stderr is a terminal (interactive run),
logs are mirrored to stderr in addition to the file. When running
as a systemd service (stderr redirected), only the file is written —
no duplicate entries.

**Mirror override:** `logger_set_mirror(int enabled)` allows explicit
control after `logger_init()`. Used by `--parse` mode: the logger is
redirected to `/dev/null` but `isatty()` returns 1 in a terminal —
`logger_set_mirror(0)` prevents CFG log lines from leaking into the
`--parse` terminal output.

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
- **Update routing:** each update is dispatched to file upload handler
  (`u->file_id != NULL`) or text command dispatcher (`u->text != NULL`).

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

SSE4.2 is guarded by `#if defined(__SSE4_2__) && defined(__x86_64__)` — if the
compiler does not support SSE4.2, the entire block is skipped and the scalar
loop handles the full buffer.

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
- **Full RELRO:** `-Wl,-z,relro,-z,now` — GOT read-only after dynamic linking
- **No CAP_SYS_BOOT:** reboot via `sudo /sbin/reboot` only
- **mlock:** TOTP secret locked in RAM, never swapped to disk
- **Upload disabled by default:** `UPLOAD_ENABLED=no` reduces attack surface
- **MarkdownV2 escaping:** handlers own escaping of user data — no silent double-escaping
- **SSH keys read-only:** `/sshkeys` shows type and comment only, key blob never exposed
- **Pending slot overwrite warning:** user notified when a new confirmation request replaces an existing one

---

## Data Flow

```
Telegram update received
    ↓
telegram_poll.c — fork() child, HTTP request via libcurl
    ↓
telegram_parser.c — JSON parsing, extract message or document
    ↓
security_is_allowed_chat() — access control for all update types
    ↓
file update? → UPLOAD_ENABLED check → cmd_handle_upload()
    ↓                                       ↓
text update? → commands.c              upload.c → disk
    ↓
validate input, rate limit, access control
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
telegram_http.c — sendMessage API call (caller owns MarkdownV2 escaping)
```

---

## CI/CD Pipelines

Three parallel build workflows — all produce fully static binaries:

| Workflow | Compiler | GLIBC | Status | Artifact |
|----------|----------|-------|--------|----------|
| `build-static.yml` | GCC 7.5.0 | 2.27 | legacy fallback | `tg-bot-static-gcc7` |
| `build-static-gcc9.yml` | GCC 9.4.0 | 2.27 | primary | `tg-bot-static-gcc9` |
| `build-static-gcc14.yml` | GCC 14 | 2.27 | active | `tg-bot-static-gcc14` |

All run in Ubuntu 18.04 Docker container to preserve glibc 2.27 compatibility.
GCC 9 and GCC 14 workflows add hardening flags: `-Wformat=2`, `-Wnull-dereference`,
`-Warray-bounds=2`, `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`.

**Binary sizes (stripped):**

| Compiler | Size |
|----------|------|
| GCC 7.5.0 | 5,735,448 B |
| GCC 9.4.0 | 5,633,048 B |
| GCC 14 | ~5,620,800 B |

---

## Design Principles

- **Minimalism** — no heavy frameworks, pure C, ~5.6 MB static binary
- **Security-first** — validate everything, least privilege, audit log
- **Predictability** — deadline timeouts, no busy-wait, no zombie window
- **Modularity** — single responsibility per module, clean public APIs
- **Operational maturity** — systemd integration, logrotate, CI/CD, build numbers
- **Backward compatibility** — TOTP is optional, classic token flow is fallback
- **Upload opt-in** — file upload disabled by default, explicit config required
- **SSH keys opt-in** — SSH_KEYS_PATH disabled by default, explicit config required
- **Caller owns escaping** — handlers responsible for correct MarkdownV2 formatting
- **Single source of truth** — compile-time paths in tg_paths.h, runtime paths in config

---

## Future Improvements

- lighttpd setup for HTTP access to uploaded files
- MarkdownV2 backtick escaping in `/fail2ban` responses (currently using plain text workaround)
- Alerts — bot notifies on service failure or high resource usage
- Log filtering by time (`--since 1h`)
- `/df` — disk usage quick view
- `/top` — top processes by CPU/RAM
- `/sshkeygen` — SSH key pair generation, saved to UPLOAD_DIR
- Unit tests — TOTP RFC 6238 vectors, security.c, logs_filter.c
- Prometheus metrics export (requires >2GB RAM)
