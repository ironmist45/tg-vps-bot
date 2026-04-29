# Architecture

This document describes the structure and organization of the project tg-bot.

## Project Layout

```
## Project Layout
tg-bot/
├── README.md              # 📘 project documentation (setup, usage, architecture)
├── Makefile               # ⚙️ build system (compile, clean, targets)
├── .gitignore             # 🚫 ignored files (build artifacts, logs, etc.)
│
├── .github/
│ └── workflows/
│ └── build-static.yml           # 🔧 CI/CD pipeline (fully static build)
│
├── config/
│ └── config.example.conf        # 🔧 example configuration (env template)
│
├── include/                     # 📂 public headers (module APIs)
│ ├── version.h                  # 🔹 version and build information
│ ├── build_info.h               # 🔹 generated build info (commit, date)
│ ├── cli.h                      # 🔹 command-line interface parsing
│ ├── config.h                   # 🔹 config loader API (parsing, reload, access)
│ ├── logger.h                   # 🔹 logging system API (levels, macros, output)
│ ├── lifecycle.h                # 🔹 process lifecycle (signals, shutdown, reboot)
│ ├── environment.h              # 🔹 runtime environment diagnostics
│ ├── exec.h                     # 🔹 execution API (command runner with timeout)
│ ├── telegram.h                 # 🔹 Telegram API client interface (public)
│ ├── telegram_http.h            # 🔹 low-level HTTP communication
│ ├── telegram_parser.h          # 🔹 JSON parsing and message formatting
│ ├── telegram_poll.h            # 🔹 long polling with fork isolation
│ ├── telegram_offset.h          # 🔹 update offset persistence
│ ├── commands.h                 # 🔹 command dispatcher interface (V2 only)
│ ├── reply.h                    # 🔹 unified response formatting API
│ ├── security.h                 # 🔹 security layer (access control, tokens)
│ ├── system.h                   # 🔹 system info (metrics, uptime, host info)
│ ├── services.h                 # 🔹 systemd services status API
│ ├── users.h                    # 🔹 active user sessions API
│ ├── logs.h                     # 🔹 logs retrieval API (journalctl integration)
│ ├── logs_filter.h              # 🔹 log filtering API (semantic + multi-keyword)
│ └── utils.h                    # 🔹 shared helpers (strings, parsing, formatting)
│
├── src/                 # 📂 implementation (core modules)
│ ├── main.c             # 🚀 entry point (init, orchestration, main loop)
│ ├── cli.c              # 🔹 command-line argument parsing
│ ├── config.c           # 🔹 config parser, reload, validation
│ ├── logger.c           # 🔹 thread-safe logging implementation
│ ├── lifecycle.c        # 🔹 signal handlers, graceful shutdown, reboot/restart
│ ├── environment.c      # 🔹 startup diagnostics and access checks
│ ├── exec.c             # 🔹 external command execution with timeout
│ ├── telegram.c         # 🔹 public API (init, send, safe polling)
│ ├── telegram_http.c    # 🔹 curl-based HTTP requests
│ ├── telegram_parser.c  # 🔹 JSON parsing + markdown escaping
│ ├── telegram_poll.c    # 🔹 long polling with fork() isolation
│ ├── telegram_offset.c  # 🔹 offset persistence (crash recovery)
│ ├── commands.c         # 🔹 command routing and dispatcher (V2 only)
│ ├── reply.c            # 🔹 response formatting helpers
│ ├── security.c         # 🔹 access control + reboot token validation
│ ├── system.c           # 🔹 system metrics and information
│ ├── services.c         # 🔹 systemd service status queries
│ ├── users.c            # 🔹 active user session enumeration
│ ├── logs.c             # 🔹 journalctl log retrieval and formatting
│ ├── logs_filter.c      # 🔹 semantic log filtering engine
│ ├── utils.c            # 🔹 helper functions implementation
│ │
│ ├── cmd_help.c         # 🧩 /help command handler (V2)
│ ├── cmd_system.c       # 🧩 /start, /status, /health, /ping, /about (V2)
│ ├── cmd_services.c     # 🧩 /services, /users, /logs (V2)
│ ├── cmd_security.c     # 🧩 /fail2ban command handler (V2)
│ └── cmd_control.c      # 🧩 /reboot, /reboot_confirm (V2)
│
├── tools/ # 🔧 internal utilities
│ └── f2b-wrapper.c      # 🔹 Fail2Ban wrapper (ban/unban/status)
│
└── build/ # 🏗 build artifacts (created by Makefile)
└── *.o                  # compiled object files
```

## Module Organization

| Module | Header | Source | Responsibility |
|--------|--------|--------|----------------|
| **CLI** | `cli.h` | `cli.c` | Command-line argument parsing, help/version output |
| **Config** | `config.h` | `config.c` | Configuration file loading, validation, reload |
| **Logger** | `logger.h` | `logger.c` | Thread-safe logging with levels and timestamps |
| **Lifecycle** | `lifecycle.h` | `lifecycle.c` | Signal handlers, graceful shutdown, reboot/restart |
| **Environment** | `environment.h` | `environment.c` | Startup diagnostics, access checks, CI detection |
| **Exec** | `exec.h` | `exec.c` | External command execution with timeout and capture |
| **Telegram** | `telegram.h` | `telegram.c` | Public API (init, shutdown, send, safe polling) |
| **Telegram HTTP** | `telegram_http.h` | `telegram_http.c` | Low-level HTTP requests via libcurl |
| **Telegram Parser** | `telegram_parser.h` | `telegram_parser.c` | JSON parsing, markdown escaping, message truncation |
| **Telegram Poll** | `telegram_poll.h` | `telegram_poll.c` | Long polling with fork() isolation and shutdown check |
| **Telegram Offset** | `telegram_offset.h` | `telegram_offset.c` | Update offset persistence (crash recovery) |
| **Commands** | `commands.h` | `commands.c` | Command routing and dispatching (V2 only) |
| **Reply** | `reply.h` | `reply.c` | Unified response formatting for handlers |
| **Security** | `security.h` | `security.c` | Access control, input validation, reboot tokens |
| **System** | `system.h` | `system.c` | System metrics, uptime, OS/hardware info |
| **Services** | `services.h` | `services.c` | Systemd service status queries |
| **Users** | `users.h` | `users.c` | Active user session enumeration |
| **Logs** | `logs.h` | `logs.c` | Journalctl log retrieval and formatting |
| **Logs Filter** | `logs_filter.h` | `logs_filter.c` | Semantic and multi-keyword log filtering |
| **Utils** | `utils.h` | `utils.c` | String manipulation, parsing, time measurement |

## Command Handlers

| Command | Handler | Source | Style |
|---------|---------|--------|-------|
| `/start` | `cmd_start_v2` | `cmd_system.c` | V2 |
| `/help` | `cmd_help_v2` | `cmd_help.c` | V2 |
| `/status` | `cmd_status_v2` | `cmd_system.c` | V2 |
| `/health` | `cmd_health_v2` | `cmd_system.c` | V2 |
| `/about` | `cmd_about_v2` | `cmd_system.c` | V2 |
| `/ping` | `cmd_ping_v2` | `cmd_system.c` | V2 |
| `/services` | `cmd_services_v2` | `cmd_services.c` | V2 |
| `/users` | `cmd_users_v2` | `cmd_services.c` | V2 |
| `/logs` | `cmd_logs_v2` | `cmd_services.c` | V2 |
| `/fail2ban` | `cmd_fail2ban_v2` | `cmd_security.c` | V2 |
| `/reboot` | `cmd_reboot_v2` | `cmd_control.c` | V2 |
| `/reboot_confirm` | `cmd_reboot_confirm_v2` | `cmd_control.c` | V2 |

---

## Overview

The project follows a modular architecture with clear separation of concerns:

- main.c — application lifecycle and loop
- telegram.c — Telegram API interaction
- commands.c — central dispatcher (routing + security + handler bridge)
- cmd_*.c — command implementations (modular)
- services.c / system.c / logs.c — domain logic
- exec.c — execution abstraction layer
- security.c — validation, rate limiting, access control

## Command Architecture (NEW)

### All commands use V2 handler model:

```
int handler_v2(command_ctx_t *ctx);
```
**command_ctx_t**
```
typedef struct {
    long chat_id;
    int user_id;
    const char *username;
    const char *args;       // raw argument string
    const char *raw_text;   // full command text
    time_t msg_date;        // message timestamp (for /ping latency)

    char *response;
    size_t resp_size;
    response_type_t *resp_type;

    unsigned short req_id;  // 16-bit request ID for log correlation
} command_ctx_t;
```
### Key Improvements (v2)
- No argc/argv — direct access to raw arguments (ctx->args)
- Clean handler signatures — less boilerplate
- Context logging with req_id and chat_id via LOG_CMD_CTX
- Unified reply API via reply_markdown(), reply_error(), reply_ok()
- Access to msg_date for latency calculation (/ping)
- Better extensibility — new fields only added to struct

### Dispatcher (commands.c)

Responsibilities:
- Input validation (security_validate_text)
- Rate limiting (security_rate_limit)
- Access control (security_check_access)
- Handler routing (V2 only)

**Legacy support removed.** The dispatcher no longer contains fallback to argc/argv handlers. All commands use V2.

### Migration Status

| Module   | Status     | Commands                                    |
|----------|------------|---------------------------------------------|
| Services | ✅ FULL v2 | /services, /users, /logs                    |
| System   | ✅ FULL v2 | /start, /status, /health, /about, /ping     |
| Help     | ✅ FULL v2 | /help                                       |
| Security | ✅ FULL v2 | /fail2ban                                   |
| Control  | ✅ FULL v2 | /reboot, /reboot_confirm                    |

**All 12 commands fully migrated to V2. Legacy code completely removed.**

### Services Module (Updated)

cmd_services.c is now fully migrated:
- /services → v2
- /users → v2
- /logs → v2
  
**Improvements:**
- Removed legacy handlers
- Direct use of ctx->args
- Input validation at handler level
- Simplified logic (no argv reconstruction)

## Execution Layer

exec.c / exec.h — centralized execution module

**Responsibilities**
fork + exec abstraction
timeout control
stdout/stderr capture
safe termination
execution diagnostics

## Execution API

### Core
```
int exec_command(
    char *const argv[],
    char *output,
    size_t size,
    const exec_opts_t *opts,
    exec_result_t *result
);
```

### Wrapper
```
int exec_command_simple(
    char *const argv[],
    char *output,
    size_t size
);
```

## Design Principles
- Minimalism
- Security-first
- Predictability
- Modular architecture
- Incremental refactoring (no big-bang rewrites)
- Backward compatibility during transitions

## Data Flow
1. Telegram update received
2. Input validation + rate limiting
3. Access control
4. Command dispatch
5. Handler execution (v2 preferred)
6. Optional system execution via exec API
7. Response sent to user

## Security Model
- Input validation at dispatcher level
- Additional validation at handler level (e.g. /logs)
- Strict access control (single-user bot)
- Controlled command execution (exec module)
- No direct shell usage

## Notes
- Uses long polling (no webhooks)
- Designed for Linux (systemd)
- Optimized for low-resource VPS
- Gradual migration from legacy handlers to v2
- Services module is fully migrated and serves as reference implementation

## Future Improvements
- Structured argument parser (replace raw ctx->args)
- Unified reply API (replace direct snprintf)
- Full migration to handler_v2
- Removal of legacy dispatcher logic
- Improved command validation layer
