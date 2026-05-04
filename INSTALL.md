# INSTALL.md

# Installation Guide

Tested on Ubuntu 18.04 LTS. Requires root or sudo access.

---

## 1. Create system user

```bash
useradd --system --no-create-home --shell /usr/sbin/nologin tg-bot
```

---

## 2. Create working directory

```bash
mkdir -p /var/lib/tg-bot
chown tg-bot:tg-bot /var/lib/tg-bot
chmod 750 /var/lib/tg-bot
```

---

## 3. Install binary

```bash
cp tg-bot /usr/local/bin/tg-bot
chown root:root /usr/local/bin/tg-bot
chmod 755 /usr/local/bin/tg-bot
```

---

## 4. Create config file

```bash
cp config/config.example.conf /etc/tg-bot.conf
chown root:tg-bot /etc/tg-bot.conf
chmod 640 /etc/tg-bot.conf
```

Edit `/etc/tg-bot.conf` and set at minimum:

```ini
BOT_TOKEN=your_telegram_bot_token
CHAT_ID=your_telegram_chat_id
LOG_FILE=/var/log/tg-bot.log
```

---

## 5. Create log file

```bash
touch /var/log/tg-bot.log
chown tg-bot:tg-bot /var/log/tg-bot.log
chmod 640 /var/log/tg-bot.log
```

---

## 6. Install logrotate config

```bash
cp tg-bot.logrotate /etc/logrotate.d/tg-bot
chown root:root /etc/logrotate.d/tg-bot
chmod 644 /etc/logrotate.d/tg-bot
```

---

## 7. Configure sudoers

The bot runs as `tg-bot` and requires sudo access for `journalctl` and `systemctl`.

Create `/etc/sudoers.d/tg-bot`:

```bash
visudo -f /etc/sudoers.d/tg-bot
```

Minimum required:

```
tg-bot ALL=(root) NOPASSWD: /usr/bin/journalctl
tg-bot ALL=(root) NOPASSWD: /bin/systemctl status *
```

---

## 8. Install Fail2Ban wrapper (optional)

```bash
cp tools/f2b-wrapper /usr/local/bin/f2b-wrapper
chown root:root /usr/local/bin/f2b-wrapper
chmod 755 /usr/local/bin/f2b-wrapper
```

Add to sudoers:

```
tg-bot ALL=(root) NOPASSWD: /usr/local/bin/f2b-wrapper
```

---

## 9. Install systemd unit

```bash
cp tg-bot.service /etc/systemd/system/tg-bot.service
systemctl daemon-reload
systemctl enable tg-bot
systemctl start tg-bot
```

---

## 10. Verify

```bash
systemctl status tg-bot
journalctl -u tg-bot -n 50
```

Expected in log:

```
[INFO ] [SYS] ==== START ====
[INFO ] [SYS] tg-bot v1.x.x
[INFO ] [SYS] Process started (PID=...)
[INFO ] [STATE] Bot started
[INFO ] [SYS] sd_notify: READY=1 sent
[INFO ] [STATE] Entering main loop
```

---

## 11. Enable TOTP 2FA (optional)

Generate a secret:

```bash
python3 -c "import base64, os; print(base64.b32encode(os.urandom(20)).decode())"
```

Add to `/etc/tg-bot.conf`:

```ini
TOTP_SECRET=YOUR_BASE32_SECRET_HERE
TOTP_SETUP=enabled
```

Reload config:

```bash
kill -HUP $(pidof tg-bot)
```

Send `/totp_setup` to the bot — scan the URI with Aegis, Google Authenticator, or Authy.

After adding to your app, disable setup mode:

```ini
TOTP_SETUP=disabled
```

Reload again:

```bash
kill -HUP $(pidof tg-bot)
```

---

## Directory layout after installation

```
/usr/local/bin/tg-bot          # binary
/etc/tg-bot.conf               # config file
/var/lib/tg-bot/               # working directory (offset, state)
/var/log/tg-bot.log            # log file
/etc/systemd/system/tg-bot.service
/etc/logrotate.d/tg-bot
/etc/sudoers.d/tg-bot
```

---

## Uninstall

```bash
systemctl stop tg-bot
systemctl disable tg-bot
rm /etc/systemd/system/tg-bot.service
systemctl daemon-reload
rm /usr/local/bin/tg-bot
rm /etc/tg-bot.conf
rm /etc/logrotate.d/tg-bot
rm /etc/sudoers.d/tg-bot
userdel tg-bot
rm -rf /var/lib/tg-bot
rm /var/log/tg-bot.log
```
