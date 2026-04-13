# tg-vps-bot

Telegram bot for VPS monitoring written in C.

---

## 📌 Overview

`tg-vps-bot` is a lightweight system administration tool that allows you to monitor and control your VPS via Telegram.

The bot runs as a service and provides access to system status, services, logs, and critical operations like reboot.

---

## 🎯 Target Platform

- Ubuntu 18.04.6 LTS
- gcc 7.x
- systemd

---

## ⚙️ Features

### 🖥 System Monitoring
- CPU load
- RAM usage
- Disk usage
- Uptime

### 🔧 Service Monitoring
- ssh.service
- shadowsocks-libev.service
- mtg.service

### 📜 Logs
- View last logs via `journalctl`

### 👤 Users
- Show logged-in users

### 🔁 Reboot
- Safe reboot with confirmation

---

## 🔐 Security

- Whitelist access via `CHAT_ID`
- No token exposure in logs
- Confirmation for critical actions

---

## 📂 Project Structure
