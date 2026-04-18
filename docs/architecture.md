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
│   ├── commands.h           # 🔹 command dispatcher interface (/logs, /services, etc.)
│   ├── logger.h             # 🔹 logging system API (levels, macros, output)
│   ├── utils.h              # 🔹 shared helpers (parsing, strings, misc)
│   ├── security.h           # 🔹 security layer (tokens, validation)
│   ├── system.h             # 🔹 system control (reboot, uptime, host info)
│   ├── services.h           # 🔹 systemd services management API
│   ├── users.h              # 🔹 user management / access control
│   ├── logs.h               # 🔹 logs retrieval API (journalctl integration)
│   ├── logs_filter.h        # 🔹 log filtering API (semantic + multi-keyword)
│   └── exec.h               # 🔹 execution API (command runner with timeout & safety)
│
├── src/                     # 📂 implementation (module sources)
│   ├── main.c               # 🚀 entry point (init, main loop)
│   ├── config.c             # 🔹 config parser implementation
│   ├── telegram.c           # 🔹 Telegram polling + request handling
│   ├── commands.c           # 🔹 command routing and handlers
│   ├── logger.c             # 🔹 logging implementation (file/stdout, formatting)
│   ├── utils.c              # 🔹 helper functions implementation
│   ├── security.c           # 🔹 auth/token validation logic
│   ├── system.c             # 🔹 system operations (reboot, metrics)
│   ├── services.c           # 🔹 systemd service control implementation
│   ├── users.c              # 🔹 user access / permissions logic
│   ├── logs.c               # 🔹 logs processing (journalctl + formatting + fallback)
│   ├── logs_filter.c        # 🔹 log filtering engine (semantic + multi-keyword)
│   └── exec.c               # 🔹 centralized command execution (NEW)
│
├── tools/                   # 🔧 internal utilities (standalone helpers)
│   └── f2b-wrapper.c        # 🔹 Fail2Ban wrapper (safe CLI bridge)
│
├── external/                # 📦 third-party dependencies
│   ├── cJSON/
│   │   ├── cJSON.c          # 🔹 JSON parser implementation
│   │   └── cJSON.h          # 🔹 JSON parser API
│
└── build/                   # 🏗 build artifacts (objects, binaries)
    └── (compiled files)
```

---

## Overview

The project follows a modular architecture with clear separation of concerns:
- main.c — application entry point, signal handling, main loop
- telegram.c — Telegram Bot API interaction (polling, sending messages)
- commands.c — command parsing and dispatch
- services.c — systemd service status handling
- system.c — system-level operations
- security.c — access control and token validation
- logger.c — logging subsystem
- config.c — configuration loading
- utils.c — helper utilities
- logs.c — log retrieval (via journalctl)
- users.c — user-related logic

## Execution Layer (NEW)

- exec.c / exec.h — centralized command execution module
- Responsibilities
- Safe process execution via fork + exec
- Timeout handling (configurable)
- Controlled stdout/stderr capture
- Signal-based termination (SIGTERM → SIGKILL)
- Execution logging (optional)
- Unified behavior across all commands

This module replaces duplicated execution logic previously located in:
- commands.c
- logs.c

## Execution API
### 1. Core function
```
int exec_command(
    char *const argv[],
    char *output,
    size_t size,
    const exec_opts_t *opts,
    exec_result_t *result
);
```

### 2. Backward-compatible wrapper
```
int exec_command_simple(
    char *const argv[],
    char *output,
    size_t size
);
```

## Options (exec_opts_t)
- timeout_ms — execution timeout
- capture_stderr — merge stderr into stdout
- log_output — enable/disable logging

## Result (exec_result_t)
- exit_code — process exit code
- timed_out — timeout flag
- signaled — terminated by signal
- bytes — output size
- duration_ms — execution time

---

## Design Principles

- Minimalism
- Predictability
- Security-first
- Separation of concerns
- Single responsibility per module
- Centralized execution control

---

## Data Flow

1. Poll Telegram API
2. Receive updates
3. Validate access
4. Process command
5. Execute system command via **exec API module** (exec.h + exec.c)
6. Send response

---

## Notes

* Uses long polling (no webhooks)
* Designed for Linux (systemd)
* Optimized for low resource usage on **low-end VPS** environments
* Execution is now fully centralized and consistent across modules
* Backward compatibility preserved via exec_command_simple()
