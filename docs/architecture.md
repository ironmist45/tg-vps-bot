# Architecture

This document describes the structure and organization of the project Tg-Bot.

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
│   └── logs.h
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
│   └── logs.c
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

The project follows a simple modular architecture:

* **main.c** — application entry point, signal handling, main loop
* **telegram.c** — Telegram Bot API interaction (polling, sending messages)
* **commands.c** — command parsing and dispatch
* **services.c** — systemd service status handling
* **system.c** — system-level operations
* **security.c** — access control and token validation
* **logger.c** — logging subsystem
* **config.c** — configuration loading
* **utils.c** — helper utilities
* **logs.c** — log retrieval
* **users.c** — user-related logic

---

## Design Principles

* Minimalism
* Predictability
* Security-first
* Separation of concerns

---

## Data Flow

1. Poll Telegram API
2. Receive updates
3. Validate access
4. Process command
5. Execute logic
6. Send response

---

## Notes

* Uses long polling (no webhooks)
* Designed for Linux (systemd)
* Optimized for low resource usage
