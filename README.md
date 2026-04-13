# tg-vps-bot

Telegram bot for VPS monitoring written in C.

---

## 📌 Overview

`tg-vps-bot` is a lightweight, fast, and secure system administration tool that allows you to monitor and control your VPS via Telegram.

The bot runs as a daemon and provides real-time access to system status, services, logs, and critical operations like reboot — directly from your phone.

---

## 🎯 Target Platform

- Ubuntu 18.04+ (tested on 18.04.6 LTS)
- gcc 7+
- systemd

---

## ⚙️ Features

### 🖥 System Monitoring
- Hostname
- Kernel version
- CPU load
- RAM usage (used / total)
- Disk usage (used / total)
- Uptime
- IP address

---

### 🔧 Service Monitoring
- ssh.service
- shadowsocks-libev.service
- mtg.service

---

### 📜 Logs
- View logs via `journalctl`
- Per-service log access

---

### 👤 Users
- Show currently logged-in users

---

### 🤖 Bot Info
- `/start` — system overview + bot info
- `/about` — version, build, uptime, PID

---

### 🔁 Reboot (Safe)
- `/reboot` generates a one-time token
- `/reboot_confirm <token>` executes reboot

---

## 🔐 Security

- ✅ Chat whitelist (`CHAT_ID`)
- 🔑 One-time tokens (invalidate after use)
- ⏱ Token TTL (e.g. 60–80 sec)
- 🚫 Rate limiting (anti-spam protection)
- 🧾 Audit logging (who triggered reboot)
- 🔍 Input validation

---

## ✨ UX Improvements

- Telegram Markdown formatting
- Clean and readable responses
- Structured command output

---

# 🔥 Что улучшено

- Добавлен `/about`
- Добавлены:
  - tokens + TTL
  - rate-limit
  - audit logs
- Описан UX (Markdown)
- Добавлена структура проекта
- README стал “GitHub-ready”

---

В планах:
- сделать **бейджи (build passing, version)**  
- оформить **релиз на GitHub**
- или сделать README как у топовых open-source проектов 😎
