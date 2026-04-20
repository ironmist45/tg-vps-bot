# Architecture

This document describes the structure and organization of the project tg-bot.

## Project Layout

```
tg-bot/
├── README.md                # 📘 project documentation (setup, usage, architecture)
├── Makefile                 # ⚙️ build system (compile, clean, targets)
├── .gitignore               # 🚫 ignored files (build artifacts, logs, etc.)
│
├── config/
│   └── config.example.conf  # 🔧 example configuration (env template)
│
├── include/                 # 📂 public headers (module APIs)
│   ├── config.h             # 🔹 config loader API (parsing + access)
│   ├── telegram.h           # 🔹 Telegram API client interface
│   ├── commands.h           # 🔹 command dispatcher interface (v1 + v2 handlers)
│   ├── logger.h             # 🔹 logging system API (levels, macros, output)
│   ├── utils.h              # 🔹 shared helpers (parsing, strings, misc)
│   ├── security.h           # 🔹 security layer (tokens, validation)
│   ├── system.h             # 🔹 system control (reboot, uptime, host info)
│   ├── services.h           # 🔹 systemd services management API
│   ├── users.h              # 🔹 user management / access control
│   ├── logs.h               # 🔹 logs retrieval API (journalctl integration)
│   ├── logs_filter.h        # 🔹 log filtering API (semantic parsing + multi-keyword filtering)
│   └── exec.h               # 🔹 execution API (command runner with timeout & safety)
│
├── src/                     # 📂 implementation (module sources)
│   ├── main.c               # 🚀 entry point (init, main loop)
│   ├── config.c             # 🔹 config parser implementation
│   ├── telegram.c           # 🔹 Telegram polling + request handling
│   ├── commands.c           # 🔹 command routing and dispatcher (v1 + v2 bridge)
│   ├── logger.c             # 🔹 logging implementation
│   ├── utils.c              # 🔹 helper functions implementation
│   ├── security.c           # 🔹 auth/token validation logic
│   ├── system.c             # 🔹 system operations (reboot, metrics)
│   ├── services.c           # 🔹 systemd service control implementation
│   ├── users.c              # 🔹 user access / permissions logic
│   ├── logs.c               # 🔹 logs processing (journalctl + formatting)
│   ├── logs_filter.c        # 🔹 log filtering engine
│   └── exec.c               # 🔹 centralized command execution
│
├── src/commands/            # 🧩 command handlers (modularized)
│   ├── cmd_system.c         # 🔹 /start, /status, /health, /ping, /about
│   ├── cmd_services.c       # 🔹 /services, /users, /logs (FULLY v2)
│   ├── cmd_help.c           # 🔹 /help
│   ├── cmd_security.c       # 🔹 /fail2ban
│   └── cmd_control.c        # 🔹 /reboot, /reboot_confirm
│
├── tools/                   # 🔧 internal utilities
│   └── f2b-wrapper.c        # 🔹 Fail2Ban wrapper
│
├── external/                # 📦 third-party dependencies
│   ├── cJSON/
│   │   ├── cJSON.c
│   │   └── cJSON.h
│
└── build/                   # 🏗 build artifacts
    └── (compiled files)
```

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
 ```
| Module   | Status     |
| -------- | ---------  |
| Services | ✅ FULL v2 |
| Users    | ✅ FULL v2 |
| Logs     | ✅ FULL v2 |
| System   | ⏳ legacy  |
| Help     | ⏳ legacy  |
| Security | ⏳ legacy  |
| Control  | ⏳ legacy  |

```

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
