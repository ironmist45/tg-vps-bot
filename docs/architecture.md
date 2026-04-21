# Architecture

This document describes the structure and organization of the project tg-bot.

## Project Layout

```
g-bot/
├── README.md # 📘 project documentation (setup, usage, architecture)
├── Makefile # ⚙️ build system (compile, clean, targets)
├── .gitignore # 🚫 ignored files (build artifacts, logs, etc.)
│
├── config/
│ └── config.example.conf # 🔧 example configuration (env template)
│
├── include/ # 📂 public headers (module APIs)
│ ├── version.h # 🔹 version and build information
│ ├── cli.h # 🔹 command-line interface parsing
│ ├── config.h # 🔹 config loader API (parsing + access)
│ ├── logger.h # 🔹 logging system API (levels, macros, output)
│ ├── lifecycle.h # 🔹 process lifecycle (signals, shutdown, reboot)
│ ├── environment.h # 🔹 runtime environment diagnostics
│ ├── exec.h # 🔹 execution API (command runner with timeout)
│ ├── telegram.h # 🔹 Telegram API client interface
│ ├── commands.h # 🔹 command dispatcher interface (v1 + v2 handlers)
│ ├── reply.h # 🔹 unified response formatting API
│ ├── security.h # 🔹 security layer (access control, tokens)
│ ├── system.h # 🔹 system info (metrics, uptime, host info)
│ ├── services.h # 🔹 systemd services status API
│ ├── users.h # 🔹 active user sessions API
│ ├── logs.h # 🔹 logs retrieval API (journalctl integration)
│ ├── logs_filter.h # 🔹 log filtering API (semantic + multi-keyword)
│ └── utils.h # 🔹 shared helpers (strings, parsing, formatting)
│
├── src/ # 📂 implementation (core modules)
│ ├── main.c # 🚀 entry point (init, orchestration, main loop)
│ ├── cli.c # 🔹 command-line argument parsing
│ ├── config.c # 🔹 config parser implementation
│ ├── logger.c # 🔹 thread-safe logging implementation
│ ├── lifecycle.c # 🔹 signal handlers, graceful shutdown, reboot/restart
│ ├── environment.c # 🔹 startup diagnostics and access checks
│ ├── exec.c # 🔹 external command execution with timeout
│ ├── telegram.c # 🔹 Telegram polling + message handling
│ ├── commands.c # 🔹 command routing and dispatcher
│ ├── reply.c # 🔹 response formatting helpers
│ ├── security.c # 🔹 access control + token validation
│ ├── system.c # 🔹 system metrics and information
│ ├── services.c # 🔹 systemd service status queries
│ ├── users.c # 🔹 active user session enumeration
│ ├── logs.c # 🔹 journalctl log retrieval and formatting
│ ├── logs_filter.c # 🔹 semantic log filtering engine
│ ├── utils.c # 🔹 helper functions implementation
│ │
│ ├── cmd_help.c # 🧩 /help command handler (v2)
│ ├── cmd_system.c # 🧩 /start, /status, /health, /ping, /about (v2)
│ ├── cmd_services.c # 🧩 /services, /users, /logs (v2)
│ ├── cmd_security.c # 🧩 /fail2ban command handler
│ └── cmd_control.c # 🧩 /reboot, /reboot_confirm
│
├── tools/ # 🔧 internal utilities
│ └── f2b-wrapper.c # 🔹 Fail2Ban wrapper (ban/unban/status)
│
├── external/ # 📦 third-party dependencies
│ └── cJSON/
│ ├── cJSON.c
│ └── cJSON.h
│
└── build/ # 🏗 build artifacts (created by Makefile)
└── *.o # compiled object files

```


## Module Organization

| Module | Header | Source | Responsibility |
|--------|--------|--------|----------------|
| **CLI** | `cli.h` | `cli.c` | Command-line argument parsing, help/version output |
| **Config** | `config.h` | `config.c` | Configuration file loading and validation |
| **Logger** | `logger.h` | `logger.c` | Thread-safe logging with levels and timestamps |
| **Lifecycle** | `lifecycle.h` | `lifecycle.c` | Signal handlers, graceful shutdown, reboot/restart |
| **Environment** | `environment.h` | `environment.c` | Startup diagnostics, access checks, CI detection |
| **Exec** | `exec.h` | `exec.c` | External command execution with timeout and capture |
| **Telegram** | `telegram.h` | `telegram.c` | Bot API communication, long polling, message sending |
| **Commands** | `commands.h` | `commands.c` | Command routing and dispatching (v1 + v2) |
| **Reply** | `reply.h` | `reply.c` | Unified response formatting for handlers |
| **Security** | `security.h` | `security.c` | Access control, input validation, reboot tokens |
| **System** | `system.h` | `system.c` | System metrics, uptime, OS/hardware info |
| **Services** | `services.h` | `services.c` | Systemd service status queries |
| **Users** | `users.h` | `users.c` | Active user session enumeration |
| **Logs** | `logs.h` | `logs.c` | Journalctl log retrieval and formatting |
| **Logs Filter** | `logs_filter.h` | `logs_filter.c` | Semantic and multi-keyword log filtering |
| **Utils** | `utils.h` | `utils.c` | String manipulation, parsing, formatting |

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
| `/fail2ban` | `cmd_fail2ban` | `cmd_security.c` | Legacy |
| `/reboot` | `cmd_reboot` | `cmd_control.c` | Legacy |
| `/reboot_confirm` | `cmd_reboot_confirm` | `cmd_control.c` | Legacy |
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

### The project now supports two handler models:

1. Legacy (v1)
```
int handler(int argc, char *argv[], ...);
```
- Uses argc/argv
- Manual parsing
- More boilerplate
- Gradually being phased out

2. Modern (v2) — Primary Direction
```
int handler_v2(command_ctx_t *ctx);
```
**command_ctx_t**
```
typedef struct {
    long chat_id;
    long user_id;
    const char *username;

    const char *args;     // raw argument string
    const char *raw_text; // full command text

    char *response;
    size_t resp_size;
    response_type_t *resp_type;
} command_ctx_t;
```
### Key Improvements (v2)
- No argc/argv
- Direct access to raw arguments (ctx->args)
- Cleaner handler signatures
- Less boilerplate
- Easier validation and parsing
- Better extensibility

### Dispatcher (commands.c)

Responsibilities:
- Input validation (security_validate_text)
- Rate limiting
- Argument splitting (legacy support)
- Access control (security_check_access)
Routing:
- handler_v2 (preferred)
- fallback to legacy handler
- Unified logging
- Response fallback handling

### Migration Status

| Module   | Status     | Commands                                    |
|----------|------------|---------------------------------------------|
| Services | ✅ FULL v2 | /services, /users, /logs                    |
| System   | ✅ FULL v2 | /start, /status, /health, /about, /ping     |
| Help     | ✅ FULL v2 | /help                                       |
| Security | ⏳ legacy  | /fail2ban                                   |
| Control  | ⏳ legacy  | /reboot, /reboot_confirm                    |

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
