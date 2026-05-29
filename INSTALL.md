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

If you plan to use `/sshkeys` (see step 13), also add:

```
tg-bot ALL=(ALL) NOPASSWD: /bin/cat /home/user/.ssh/authorized_keys
```

Replace `/home/user/.ssh/authorized_keys` with the actual path to your
`authorized_keys` file. The path must be explicit — no wildcards.

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

Before starting the bot, validate the config file:

```bash
tg-bot --parse /etc/tg-bot.conf
```

Expected output (green checkmarks for all required fields):

```
tg-bot v1.x.x (Codename) — config parser
File: /etc/tg-bot.conf

[REQUIRED]
  ✓  TOKEN                  set (46 chars)
  ✓  CHAT_ID                123456789
...
Config OK — 0 errors, 2 warnings
```

> Warnings for `LOG_FILE dir` and `UPLOAD_DIR` are expected when running
> as a regular user — those paths are owned by `tg-bot` at runtime.

Then check the service is running:

```bash
systemctl status tg-bot
journalctl -u tg-bot -n 50
```

Expected in log:

```
[ INFO ] [ SYS ] ==== START ====
[ INFO ] [ SYS ] tg-bot v1.x.x (Codename)
[ INFO ] [ SYS ] Process started (Main PID=...)
[ INFO ] [ CFG ] UPLOAD: disabled
[ INFO ] [ CFG ] SSH_KEYS_PATH: disabled
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

## 12. Enable file upload (optional)

File upload is disabled by default. To enable it:

**Create the upload directory:**

```bash
mkdir -p /var/www/html/uploads
chown tg-bot:tg-bot /var/www/html/uploads
chmod 750 /var/www/html/uploads
```

**Add to `/etc/tg-bot.conf`:**

```ini
UPLOAD_ENABLED=yes
UPLOAD_DIR=/var/www/html/uploads
```

Reload config:

```bash
kill -HUP $(pidof tg-bot)
```

After this, files sent to the bot via Telegram (up to 20 MB) will be saved
to the upload directory. Use `/files` to list uploaded files.

> **Note:** The upload directory is designed to work as a lighttpd document
> root so files become accessible via HTTP once lighttpd is configured.
> Without lighttpd files are saved to disk but not served over HTTP.

---

## 13. Enable SSH keys listing (optional)

The `/sshkeys` command lists authorized SSH public keys (type and comment only —
the key blob is never shown). Disabled by default.

**Add the sudoers entry** (if not done in step 7):

```bash
visudo -f /etc/sudoers.d/tg-bot
```

Add:

```
tg-bot ALL=(ALL) NOPASSWD: /bin/cat /home/user/.ssh/authorized_keys
```

**Add to `/etc/tg-bot.conf`:**

```ini
SSH_KEYS_PATH=/home/user/.ssh/authorized_keys
```

Reload config:

```bash
kill -HUP $(pidof tg-bot)
```

Send `/sshkeys` to the bot to verify.

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
/var/www/html/uploads/                 # upload directory (optional)
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
rm -rf /var/www/html/uploads
rm /var/log/tg-bot.log
```
