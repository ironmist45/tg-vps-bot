# Architecture

This document describes the structure and organization of the project tg-bot.

## Project Layout

```
tg-bot/
├── README.md
├── Makefile
├── .gitignore
│
├── config/
│   └── config.example.conf
│
├── include/
│   ├── config.h
│   ├── telegram.h
│   ├── commands.h
│   ├── logger.h
│   ├── utils.h
│   ├── security.h
│   ├── system.h 
│   ├── services.h
│   ├── users.h
│   ├── logs.h
│   └── exec.h          # 🔹 execution API (NEW)
│
├── src/
│   ├── main.c
│   ├── config.c
│   ├── telegram.c
│   ├── commands.c
│   ├── logger.c
│   ├── utils.c
│   ├── security.c
│   ├── system.c
│   ├── services.c
│   ├── users.c
│   ├── logs.c
│   └── exec.c          # 🔹 centralized command execution (NEW)
│
├── external/
│   ├── cJSON/
│   │   ├── cJSON.c
│   │   └── cJSON.h
│   │
│   └── f2b-wrapper/
│       └── (external dependency - Fail2Ban integration!)
│
└── build/
    └── (compiled binaries and object files)
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

## Core function
int exec_command(
    char *const argv[],
    char *output,
    size_t size,
    const exec_opts_t *opts,
    exec_result_t *result
);

## Backward-compatible wrapper
int exec_command_simple(
    char *const argv[],
    char *output,
    size_t size
);

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
