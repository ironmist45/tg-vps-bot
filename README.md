# 🚀 Telegram Server Bot ![Build Status](https://github.com/ironmist45/tg-vps-bot/actions/workflows/build-static.yml/badge.svg)

Production-ready Telegram bot for server monitoring and management, written in pure C.

---

## ✨ Features

### 🖥 System Monitoring

* CPU load (1 / 5 / 15 min)
* Memory usage (MB + %)
* Disk usage (GB + %)
* Uptime
* Active users

---

### ⚙️ Service Management

* Check system services status via `systemctl`

Supports the following services:

- SSH
- Shadowsocks
- MTG MTPROTO proxy for Telegram  
  by Sergei Arkhipov aka 9seconds  
  https://github.com/9seconds/mtg

* Clean formatted output with status indicators:

  * 🟢 UP
  * 🔴 DOWN
  * 🟡 FAIL / STARTING
  * ⚪ UNKNOWN

---

### 📜 Logs & Users

* View service logs via `journalctl` (`/logs`)
* Semantic log filtering: `error`, `auth`, `brute`, `ip`, `session`, `pam`
* Multi-keyword AND filtering (e.g. `/logs ssh error`)
* Inspect active user sessions
* Configurable line count (default 30, max 200)

---

### 📊 Metrics & Health Monitoring

Built-in metrics collected since last bot start:

**Commands:**
- Total commands processed
- Per-command counters (`/start`, `/help`, `/logs`, `/fail2ban`, `/reboot`, `/restart`, `/services`, `/users`, `/health`)

**Errors:**
- Unauthorized access attempts
- Polling timeouts
- Exec failures (sudo, systemctl, journalctl)

**Performance:**
- Average / max response time (ms)
- Poll cycles count
- Telegram API calls (total / failed)

**How to check:** `/health` — compact view with system status + bot metrics

**On shutdown**, full metrics summary is written to the log:
```
[SYS] Commands: 42 (start=1 help=2 logs=15 ...) | Errors: unauthorized=0 timeout=0 exec=0 | Response: avg=142ms max=570ms
```

---

### 🛡 Security

* 🔐 **Single allowed chat_id** — only one Telegram user can control the bot
* 🚫 All unauthorized access attempts are logged:
  ```
  poll=01a3 req=2212 ACCESS CHECK: chat_id=123456789 cmd=/start result=DENIED
  ```
* 🔑 **Two-step confirmation** for dangerous commands (`/reboot`, `/restart`)
  * Stateless time-based token
  * Configurable TTL
  * Bruteforce protection (blocks after 5 failed attempts)
  * Replay protection (single-use token)
* 🧪 Input validation:
  * Message length limit
  * IP address validation (Fail2Ban)
  * Character whitelist for command arguments

---

### 🔥 Fail2Ban Integration

* Check jail status (`/fail2ban status`, `/fail2ban status sshd`)
* Ban / unban IP addresses
* Secure wrapper execution via `f2b-wrapper`
* Full audit logging

---

### 🔄 Process Control

* `/restart` — restart the bot process (systemctl restart tg-bot)
* `/reboot` — reboot the server
* Both require two-step token confirmation
* Tracks who requested the operation

---

### ⚡ Telegram Engine

* Long polling (no webhooks)
* Fork isolation for libcurl — network hangs cannot block the main process
* Offset persistence with atomic write (crash recovery, no duplicate messages)
* Runtime duplicate detection
* MarkdownV2 formatting with automatic escaping
* Automatic message truncation (4096 char limit)

---

### 🧠 Reliability & Stability

* Safe JSON parsing — no crashes on malformed Telegram responses
* NULL checks everywhere
* Deadline-based curl timeouts (no busy-wait)
* Zero zombie processes — blocking waitpid after pipe EOF
* Early log buffer — startup messages are captured before log file opens
* Graceful shutdown:
  * Saves update offset to disk
  * Flushes logs
  * Syncs filesystem
* systemd watchdog support (`Type=notify`, `WatchdogSec=60`)
* Log rotation support via `SIGUSR1`
* Main loop iteration timing — warns if iteration exceeds 35s

---

### 📊 Logging System

* Levels: `DEBUG` / `INFO` / `WARN` / `ERROR`
* Millisecond-precision timestamps
* Complete log from process start — early messages buffered and flushed to file on open
* Mirror to stderr when running interactively (isatty detection)
* No stderr duplication when running as systemd service
* Thread-safe writes via mutex
* Safe formatting — no crashes on bad input

---

## 🖥️ CLI Interface

### ▶️ Run

```bash
tg-bot --config /etc/tg-bot/config.conf
```

### ⚙️ Setup (first run)

```bash
cp config/config.example.conf config/config.conf
# edit config.conf — set TOKEN and CHAT_ID
```

### 🔧 Options

* `-c`, `--config <path>` — config file path
* `-h`, `--help` — show help
* `-v`, `--version` — show version with build info

---

## 📦 Example Commands

```
General
/start   — welcome message with system summary
/help    — list all commands

System info
/status  — detailed system status (CPU, memory, disk, uptime)
/health  — compact health check + bot metrics
/about   — bot version, build info, library versions
/ping    — latency test (processing time, inbound, RTT)

Services
/services              — status of all monitored services
/users                 — active login sessions
/logs ssh              — last 30 lines from ssh journal
/logs ssh 50           — last 50 lines
/logs ssh error        — filtered by "error" (semantic)
/logs ssh 100 brute    — last 100 lines filtered for brute-force patterns

Security
/fail2ban status           — show all jails
/fail2ban status sshd      — show sshd jail
/fail2ban ban 1.2.3.4      — ban IP
/fail2ban unban 1.2.3.4    — unban IP

System control (two-step confirmation required)
/reboot   — reboot the server
/restart  — restart the bot process
```

---

## 🏗 Architecture

Modular C design — each module has a single responsibility:

* `main.c` — orchestration, main event loop, signal handling
* `lifecycle.c` — process lifecycle (signals, shutdown, reboot, restart)
* `telegram_poll.c` — long polling with fork() isolation for libcurl
* `telegram_http.c` — low-level HTTP via libcurl
* `telegram_parser.c` — JSON parsing, markdown escaping
* `telegram_offset.c` — update offset persistence
* `commands.c` — command dispatcher and routing
* `security.c` — access control, rate limiting, token validation
* `config.c` — configuration file parsing and reload
* `logger.c` — thread-safe logging with early buffer
* `diagnostics.c` — runtime diagnostics (loop timing)
* `services_config.c` — shared service definitions (single source of truth)
* `services.c` — systemd service status queries
* `system.c` — system metrics (CPU, memory, disk, uptime)
* `exec.c` — external command execution with deadline timeout
* `logs.c` + `logs_filter.c` — journalctl integration and semantic filtering
* `users.c` — active session enumeration via utmp
* `metrics.c` — bot usage statistics
* `utils.c` — shared helpers

---

## 🧪 Production-Ready Details

* Offset saved atomically via temp file + rename (no corruption on crash)
* `pipe2(O_CLOEXEC)` — no fd leaks into child processes
* Deadline-based `select()` — total timeout is always exactly `timeout_ms`
* Blocking `waitpid()` after pipe EOF — no zombie window
* `getrandom(2)` for token salt — cryptographically unpredictable
* All timeout constants in `telegram_timeouts.h` with `_Static_assert` chain
* Config reload on `SIGHUP` without restart
* Log reopen on `SIGUSR1` for logrotate integration

---

## 🔐 Security Notes

* Bot responds **only to one chat_id** — hardcoded in config
* All unauthorized attempts are logged with chat_id
* No shell injection — IP validation via `inet_pton`, service names via character whitelist
* No direct root commands — all privileged operations via sudo whitelist
* Token salt generated via `getrandom(2)` at startup — unpredictable across restarts

---

## ⚠️ Requirements

> Primarily tested on:

* **OS:** Ubuntu 18.04.6 LTS x86_64
* **Compiler:** GCC 7.5.0
* **Libraries (all statically linked):**
  * 🌐 **libcurl 8.20.0** — HTTP client (curl license)
  * 🔒 **OpenSSL 3.0.20** — TLS/SSL (Apache 2.0)
  * 🧭 **c-ares 1.34.6** — async DNS resolver (MIT)
  * 📄 **cJSON 1.7.19** — JSON parser (MIT)

> ⚡ The binary is fully self-contained (~5 MB). Only `libc` is required at runtime.

### 📦 Install

No dependencies required on the target system:

```bash
chmod +x tg-bot
sudo cp tg-bot /usr/local/bin/
```

---

## Build Notes

Built in Ubuntu 18.04 environment using Docker for reproducibility.

**Linking model:**
- Fully static: libcurl, OpenSSL, c-ares, cJSON compiled from source
- Only `libc` (glibc) remains dynamically linked

⚠️ Do **NOT modify** linker flags unless you know what you are doing.

---

## Optional Dependencies

`f2b-wrapper` — required only for `/fail2ban` commands.  
Source: `tools/f2b-wrapper.c`

> The `f2b-wrapper` binary **must be built on the target system**.  
> Using a binary compiled elsewhere may cause `GLIBC_X.Y not found` errors.

If you see this at startup:
```
[ERROR] [SYS] fail2ban-wrapper: FAIL (EXEC_FAILED)
```
Rebuild `f2b-wrapper` locally on the target machine.

---

## 🚧 Roadmap

* `/service start|stop|restart` — service management via Telegram
* TOTP (2FA) for dangerous commands — Google Authenticator / Authy
* Alerts — bot notifies on service failure or high CPU
* Prometheus metrics export
* Log filtering by time (`--since 1h`)

---

## Philosophy

Built with focus on reliability, control and independence.  
Inspired by the idea that infrastructure should remain in your hands.

> Stay independent. 🙂

---

## 📄 License ![License](https://img.shields.io/badge/license-MIT-green)

MIT © ironmist45
