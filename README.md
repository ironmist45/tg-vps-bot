# 🚀 Telegram Server Bot (pure C) ![Build](https://github.com/ironmist45/tg-vps-bot/actions/workflows/build.yml/badge.svg)

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
    
* Clean formatted output with the following status indicators:

  * 🟢 UP
  * 🔴 DOWN
  * 🟡 FAIL

---

### 📜 Logs & Users

* View service logs (`/logs`)
* Inspect active users
* Flexible arguments support

---

### 🛡 Security (PRO level)

* 🔐 **Single allowed chat_id**
* 🚫 All unauthorized access is logged:

  ```
  ACCESS DENIED: chat_id=... text=...
  ```
* 🔑 **Stateless reboot tokens**

  * Time-based validation
  * Configurable TTL
* 🧪 Input validation:

  * Message length limit
  * IP validation (Fail2Ban)

---

### 🔥 Fail2Ban Integration

* Check jail status
* Ban / unban IPs
* Secure wrapper execution
* Full audit logging:

  ```
  FAIL2BAN BAN: 1.2.3.4 (chat_id=...)
  ```

---

### 🔄 Process Control

* Restart bot via Telegram
* Reboot server (with confirmation token)
* Tracks who requested reboot

---

### ⚡ Telegram Engine

* Long polling (no webhooks)
* Offset persistence (anti-duplicate)
* Runtime duplicate protection
* MarkdownV2 formatting
* Automatic message truncation (4096 limit)

---

### 🧠 Reliability & Stability

* Safe JSON parsing (no crashes on invalid data)
* NULL checks everywhere
* Curl timeouts & keepalive
* Graceful shutdown:

  * Stops Telegram
  * Flushes logs
  * Syncs filesystem
* Offset write optimization (reduces disk load)

---

### 📊 Logging System

* Levels: DEBUG / INFO / WARN / ERROR
* Timestamped logs
* File + stderr fallback
* Safe formatting (no crashes on bad input)
* Response preview (clean, no emoji/markdown)

---

## 🖥️ CLI Interface

### ▶️ Run

```bash
tg-bot --config /etc/tg-bot/config.conf
```

### ⚙️ Setup (first run, pls edit & check your config!)

```bash
cp config/config.example.conf config/config.conf
```

### 🔧 Options

* `-c`, `--config <path>` — config file path
* `-h`, `--help` — show help
* `-v`, `--version` — show version

---

## 🏗 Architecture

Modular C design:

* `main.c` — lifecycle & signals
* `telegram.c` — API communication
* `commands.c` — command dispatcher
* `system.c` — system stats
* `services.c` — service monitoring
* `security.c` — auth & tokens
* `logger.c` — logging
* `utils.c` — helpers

---

## 🧪 Production-Ready Details

* Offset saved safely via temp file + rename
* Protection against malformed Telegram updates
* No memory leaks in polling loop
* Handles network timeouts gracefully
* Safe string operations (no overflows)

---

## 🔐 Security Notes

* Bot responds **only to one chat_id**
* All other attempts are logged
* No shell injection (IP validation)
* No direct root commands (wrapper used)

---

## 📦 Example Commands

```
**General**
/start - Start bot
/help  - Help
**System info**
/status - System status
/health - Health check
/about  - About tg-bot
/ping   - Ping
**Services**
/services - Services statuses (ssh, mtg, shadowsocks)
/users    - Show users
/logs     - Services logs (example commands: /logs ssh, /logs mtg, logs shadowsocks, /logs <service> <N>, /logs <service> error)
**Security**
/fail2ban status - Fail2Ban (example commands: /fail2ban status, /fail2ban status sshd, /fail2ban ban <ip>, /fail2ban unban <ip>)
**System**
/reboot - System reboot
```

---

## 🧠 What Makes It Special

* Written in pure C (no heavy frameworks)
* Extremely lightweight
* Full control over memory and performance
* Designed for real server environments

---

## 🚧 Future Improvements

* Rate limiting (anti-spam)
* Chat blacklist / whitelist
* Metrics export (Prometheus)
* Async request handling
* Multi-user roles

---

## ⚠️ Requirements

> This project is tested on a **specific environment!**
> Primarily tested on the following environment:

* **OS:** Ubuntu 18.04.6
* **Compiler:** GCC 7.5.0
* **Libraries:**
    - **🌐 libcurl** — HTTP client library (License: curl license (MIT-like), Website: https://curl.se/libcurl/)
    - **📄 cJSON** — lightweight JSON parser (License: MIT, Author: Dave Gamble, repo: https://github.com/DaveGamble/cJSON, version: v1.7.15)

> ⚡ Other environments may work, but are not guaranteed.

### 📦 Install dependencies (Ubuntu 18.04)

```bash
sudo apt update
sudo apt install build-essential libcurl4-openssl-dev libcjson-dev
```

### 🔍 Check compiler version

```bash
gcc --version
```

Expected:

```
gcc (Ubuntu 7.5.0-3ubuntu1~18.04) 7.5.0
```

## Build Notes

Project is built in Ubuntu 18.04 environment using Docker.

Linking model:
- cJSON: static
- libcurl / OpenSSL: dynamic

⚠️ Do **NOT modify** linker flags unless you know what you are doing!

## Optional Dependencies

- f2b-wrapper — required only for fail2ban commands (pls, see the source code in /tools directory of this repo).

### fail2ban wrapper integration note

The `f2b-wrapper` binary **must be** built on the target system.

Using binaries compiled on a different system may result in runtime errors such as:

    GLIBC_X.Y not found

If you see during tg-bot start the following message:

    [ERROR] [SYS] fail2ban-wrapper: FAIL (no output, EXIT_NONZERO)

then, pls kindly rebuild `f2b-wrapper` locally.

## Philosophy

Built with focus on reliability, control and independence.

Inspired by the idea that infrastructure should remain in your hands.

> Stay independent. 🙂

## 📄 License ![License](https://img.shields.io/badge/license-MIT-green)

MIT © ironmist45
