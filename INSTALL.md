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

No special capabilities are required — all privileged operations go through sudo.

---

## 4. Create config file

```bash
cp config/config.example.conf /etc/tg-bot.conf
chown root:tg-bot /etc/tg-bot.conf
chmod 640 /etc/tg-bot.conf
```

Edit `/etc/tg-bot.conf` and set at minimum:

```ini
TOKEN=your_telegram_bot_token
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

The bot runs as `tg-bot` and requires sudo access for system operations.
Create `/etc/sudoers.d/tg-bot`:

```bash
visudo -f /etc/sudoers.d/tg-bot
```

Add the following (adjust service names to match your setup):

```
# tg-bot
tg-bot ALL=(ALL) NOPASSWD: \
/sbin/reboot, \
/bin/journalctl, \
/bin/systemctl restart tg-bot, \
/bin/systemctl is-active *, \
/bin/systemctl start ssh, \
/bin/systemctl stop ssh, \
/bin/systemctl restart ssh, \
/bin/systemctl start shadowsocks-libev, \
/bin/systemctl stop shadowsocks-libev, \
/bin/systemctl restart shadowsocks-libev, \
/bin/systemctl start mtg, \
/bin/systemctl stop mtg, \
/bin/systemctl restart mtg, \
/usr/local/bin/f2b-wrapper *
```

Verify syntax:

```bash
visudo -c -f /etc/sudoers.d/tg-bot
```

> **Note:** Adjust the service list to match the services defined in `services_config.c`.
> Add or remove `start`/`stop`/`restart` entries for each service you want to manage via `/service`.

---

## 8. Install Fail2Ban wrapper (optional)

Required only for `/fail2ban` commands. Build on the target machine to avoid
GLIBC version mismatches:

```bash
gcc -O2 -Wall -Wextra -std=c11 tools/f2b-wrapper.c -o f2b-wrapper
strip f2b-wrapper
cp f2b-wrapper /usr/local/bin/f2b-wrapper
chown root:root /usr/local/bin/f2b-wrapper
chmod 755 /usr/local/bin/f2b-wrapper
```

The `/usr/local/bin/f2b-wrapper *` sudoers entry (added in step 7) covers this.

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
[ INFO ] [ SYS ] ==== START ====
[ INFO ] [ SYS ] tg-bot v1.x.x (Codename)
[ INFO ] [ SYS ] Process started (Main PID=...)
[ INFO ] [STATE] Bot started
[ INFO ] [ SYS ] sd_notify: READY=1 sent
[ INFO ] [STATE] Entering main loop
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
/usr/local/bin/tg-bot                  # main binary
/usr/local/bin/f2b-wrapper             # Fail2Ban wrapper (optional)
/etc/tg-bot.conf                       # config file
/var/lib/tg-bot/                       # working directory (offset, state)
/var/log/tg-bot.log                    # log file
/etc/systemd/system/tg-bot.service     # systemd unit
/etc/logrotate.d/tg-bot                # logrotate config
/etc/sudoers.d/tg-bot                  # sudo permissions
```

---

## Uninstall

```bash
systemctl stop tg-bot
systemctl disable tg-bot
rm /etc/systemd/system/tg-bot.service
systemctl daemon-reload
rm /usr/local/bin/tg-bot
rm -f /usr/local/bin/f2b-wrapper
rm /etc/tg-bot.conf
rm /etc/logrotate.d/tg-bot
rm /etc/sudoers.d/tg-bot
userdel tg-bot
rm -rf /var/lib/tg-bot
rm /var/log/tg-bot.log
```
