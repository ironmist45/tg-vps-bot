# Architecture

This document describes the structure and organization of the project.

## Project Layout

tg-bot/
├── README.md
├── Makefile
├── .gitignore
│
├── config/
│ └── config.example.conf
│
├── include/
│ ├── config.h
│ ├── telegram.h
│ ├── commands.h
│ ├── logger.h
│ ├── utils.h
│ ├── security.h
│ ├── system.h
│ ├── services.h
│ ├── users.h
│ └── logs.h
│
├── src/
│ ├── main.c
│ ├── config.c
│ ├── telegram.c
│ ├── commands.c
│ ├── logger.c
│ ├── utils.c
│ ├── security.c
│ ├── system.c
│ ├── services.c
│ ├── users.c
│ └── logs.c
│
├── external/
│ └── cJSON/
│ ├── cJSON.c
│ └── cJSON.h
│
└── build/
└── (compiled binaries and object files)


---

## Overview

The project follows a simple modular architecture:

- **main.c** — application entry point, signal handling, main loop  
- **telegram.c** — Telegram Bot API interaction (polling, sending messages)  
- **commands.c** — command parsing and dispatch  
- **services.c** — systemd service status handling  
- **system.c** — system-level operations (e.g. reboot)  
- **security.c** — access control and token validation  
- **logger.c** — logging subsystem  
- **config.c** — configuration loading  
- **utils.c** — helper utilities  
- **logs.c** — log retrieval  
- **users.c** — user-related logic (if extended)

---

## Design Principles

- **Minimalism** — no unnecessary abstractions or dependencies  
- **Predictability** — explicit control over behavior  
- **Security-first** — whitelist + token validation  
- **Separation of concerns** — each module has a clear responsibility  

---

## Data Flow

1. Bot polls Telegram API (`telegram.c`)
2. Receives updates (messages)
3. Filters by `chat_id` (`security.c`)
4. Passes text to command handler (`commands.c`)
5. Executes logic (services/system/etc.)
6. Sends response back via Telegram API

---

## Notes

- Uses long polling (no webhooks)
- Designed for Linux systems (systemd-based)
- Optimized for low resource usage
