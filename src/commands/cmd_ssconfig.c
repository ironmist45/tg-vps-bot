/**
 * tg-bot - Telegram bot for system administration
 * cmd_ssconfig.c - /ssconfig command handler (V2)
 *
 * Generates Shadowsocks + Cloak client config files from server-side
 * configuration. Reads config.json and ckserver.json, combines with
 * CLOAK_PUBLIC_KEY and auto-detected server IPv4, writes two JSON
 * files to UPLOAD_DIR.
 *
 * MIT License - Copyright (c) 2026 ironmist45
 */

#include "cmd_ssconfig.h"
#include "config.h"
#include "logger.h"
#include "reply.h"
#include "metrics.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cjson/cJSON.h>

/* Max length for generated file paths */
#define SSCONFIG_PATH_MAX 512

/* Extra bytes for ".tmp" suffix in atomic write temp path */
#define SSCONFIG_TMP_SUFFIX_LEN 4

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

/**
 * Get first non-loopback IPv4 address via getifaddrs(3).
 *
 * Iterates network interfaces and returns the first AF_INET address
 * that is not on the loopback interface (127.x.x.x).
 *
 * Replaces the previous popen("hostname -I") implementation which
 * depended on shell availability and PATH. getifaddrs(3) is a direct
 * system call with no external dependencies.
 *
 * @param out   Output buffer (receives dotted-decimal IPv4 string)
 * @param size  Buffer size (INET_ADDRSTRLEN = 16 is sufficient)
 * @return      0 on success, -1 if no suitable address found
 */
static int get_server_ipv4(char *out, size_t size)
{
    struct ifaddrs *ifaddr = NULL;

    if (getifaddrs(&ifaddr) != 0)
        return -1;

    int found = 0;

    for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr)
            continue;

        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;

        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
        uint32_t ip = ntohl(addr->sin_addr.s_addr);

        /* Skip loopback (127.0.0.0/8) */
        if ((ip >> 24) == 127)
            continue;

        if (inet_ntop(AF_INET, &addr->sin_addr, out, (socklen_t)size)) {
            found = 1;
            break;
        }
    }

    freeifaddrs(ifaddr);
    return found ? 0 : -1;
}

/**
 * Read entire file into a heap-allocated buffer.
 * Caller must free() the returned pointer.
 * Returns NULL on error.
 */
static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz <= 0 || sz > 65536) { fclose(fp); return NULL; } /* sanity cap */
    rewind(fp);

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }

    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);

    buf[n] = '\0';
    return buf;
}

/**
 * Read a string field from a JSON file on disk.
 *
 * @param path   Path to JSON file
 * @param field  Field name (case-sensitive)
 * @param out    Output buffer
 * @param size   Output buffer size
 * @return       0 on success, -1 on error
 */
static int json_read_string(const char *path, const char *field,
                             char *out, size_t size)
{
    char *raw = read_file(path);
    if (!raw) return -1;

    cJSON *json = cJSON_Parse(raw);
    free(raw);
    if (!json) return -1;

    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, field);
    int rc = -1;

    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(out, size, "%s", item->valuestring);
        rc = 0;
    }

    cJSON_Delete(json);
    return rc;
}

/**
 * Read an integer field from a JSON file on disk.
 *
 * @param path   Path to JSON file
 * @param field  Field name (case-sensitive)
 * @param out    Output integer
 * @return       0 on success, -1 on error
 */
static int json_read_int(const char *path, const char *field, int *out)
{
    char *raw = read_file(path);
    if (!raw) return -1;

    cJSON *json = cJSON_Parse(raw);
    free(raw);
    if (!json) return -1;

    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, field);
    int rc = -1;

    if (cJSON_IsNumber(item)) {
        *out = item->valueint;
        rc = 0;
    }

    cJSON_Delete(json);
    return rc;
}

/**
 * Read the first string element of a JSON array field.
 *
 * Used for BypassUID which is stored as ["uid-string"].
 *
 * @param path   Path to JSON file
 * @param field  Array field name
 * @param out    Output buffer
 * @param size   Output buffer size
 * @return       0 on success, -1 on error
 */
static int json_read_array_first(const char *path, const char *field,
                                  char *out, size_t size)
{
    char *raw = read_file(path);
    if (!raw) return -1;

    cJSON *json = cJSON_Parse(raw);
    free(raw);
    if (!json) return -1;

    cJSON *arr = cJSON_GetObjectItemCaseSensitive(json, field);
    int rc = -1;

    if (cJSON_IsArray(arr)) {
        cJSON *first = cJSON_GetArrayItem(arr, 0);
        if (cJSON_IsString(first) && first->valuestring) {
            snprintf(out, size, "%s", first->valuestring);
            rc = 0;
        }
    }

    cJSON_Delete(json);
    return rc;
}

/**
 * Write a cJSON object to a file atomically via tmp + rename.
 *
 * Writes to <path>.tmp first, then rename(2) to <path>.
 * rename(2) is atomic on POSIX filesystems when both paths are on
 * the same filesystem — guaranteed here since tmp is in the same
 * directory as the final file (same UPLOAD_DIR).
 *
 * If the process crashes after writing .tmp but before rename,
 * the .tmp file is left behind but the final file is untouched.
 * On next run a new timestamp is used so there is no collision.
 *
 * @param path  Destination file path
 * @param json  cJSON object to serialise
 * @return      0 on success, -1 on error
 */
static int write_json_file_atomic(const char *path, cJSON *json)
{
    char tmp_path[SSCONFIG_PATH_MAX + SSCONFIG_TMP_SUFFIX_LEN];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    char *str = cJSON_Print(json);
    if (!str) return -1;

    FILE *fp = fopen(tmp_path, "w");
    if (!fp) {
        free(str);
        return -1;
    }

    int write_ok = (fputs(str, fp) != EOF);
    fclose(fp);
    free(str);

    if (!write_ok) {
        remove(tmp_path);
        return -1;
    }

    if (rename(tmp_path, path) != 0) {
        remove(tmp_path);
        return -1;
    }

    return 0;
}

// ============================================================================
// /ssconfig COMMAND (V2)
// ============================================================================

/**
 * Generate Shadowsocks + Cloak client config files.
 *
 * Flow:
 *   1. Check prerequisites (CLOAK_PUBLIC_KEY, UPLOAD_ENABLED)
 *   2. Detect server IPv4 via getifaddrs(3)
 *   3. Read server_port, password, method from config.json
 *   4. Read BypassUID, RedirAddr from ckserver.json
 *   5. Write cloak-client-<ts>.json to UPLOAD_DIR (atomic)
 *   6. Write ss-client-<ts>.json to UPLOAD_DIR (atomic)
 *   7. Reply with summary and download instructions
 *
 * @param ctx  Command context
 * @return     0 on success, -1 on error
 */
int cmd_ssconfig_v2(command_ctx_t *ctx)
{
    /* Prerequisite: CLOAK_PUBLIC_KEY must be configured */
    if (g_cfg.cloak_public_key[0] == '\0') {
        return reply_plain(ctx,
            "ℹ️ Shadowsocks+Cloak config not configured.\n\n"
            "Add to config:\n"
            "CLOAK_PUBLIC_KEY=your_curve25519_public_key\n\n"
            "To get the key pair:\n"
            "  ck-server -key\n\n"
            "Then reload: kill -HUP $(pidof tg-bot)");
    }

    /* Prerequisite: UPLOAD_DIR needed to save generated files */
    if (!g_cfg.upload_enabled || g_cfg.upload_dir[0] == '\0') {
        return reply_plain(ctx,
            "ℹ️ File upload not configured.\n\n"
            "Add to config:\n"
            "UPLOAD_ENABLED=yes\n"
            "UPLOAD_DIR=/var/www/html/uploads");
    }

    LOG_CMD_CTX(ctx, LOG_INFO, "ssconfig: generating client configs");

    /* ------------------------------------------------------------------ */
    /* 1. Detect server IPv4                                               */
    /* ------------------------------------------------------------------ */
    char server_ip[INET_ADDRSTRLEN] = {0};
    if (get_server_ipv4(server_ip, sizeof(server_ip)) != 0) {
        LOG_CMD_CTX(ctx, LOG_WARN, "ssconfig: failed to get server IPv4");
        return reply_error(ctx,
            "Failed to determine server IP\n"
            "No non-loopback IPv4 interface found");
    }
    LOG_CMD_CTX(ctx, LOG_DEBUG, "ssconfig: server_ip=%s", server_ip);

    /* ------------------------------------------------------------------ */
    /* 2. Read shadowsocks config.json                                     */
    /* ------------------------------------------------------------------ */
    int  ss_port          = 0;
    char ss_password[128] = {0};
    char ss_method[64]    = {0};

    if (json_read_int(g_cfg.cloak_ss_config, "server_port", &ss_port) != 0) {
        LOG_CMD_CTX(ctx, LOG_WARN, "ssconfig: failed to read server_port from %s",
                    g_cfg.cloak_ss_config);
        return reply_error(ctx,
            "Failed to read SS config\n"
            "Check CLOAK_SS_CONFIG path");
    }

    if (json_read_string(g_cfg.cloak_ss_config, "password",
                         ss_password, sizeof(ss_password)) != 0) {
        LOG_CMD_CTX(ctx, LOG_WARN, "ssconfig: failed to read password");
        return reply_error(ctx, "Failed to read SS password");
    }

    /* method has a sensible fallback if missing */
    if (json_read_string(g_cfg.cloak_ss_config, "method",
                         ss_method, sizeof(ss_method)) != 0) {
        safe_copy(ss_method, sizeof(ss_method), "chacha20-ietf-poly1305");
    }

    /* ------------------------------------------------------------------ */
    /* 3. Read ckserver.json                                               */
    /* ------------------------------------------------------------------ */
    char ck_uid[128]        = {0};
    char ck_redir_addr[256] = {0};

    if (json_read_array_first(g_cfg.cloak_ck_config, "BypassUID",
                               ck_uid, sizeof(ck_uid)) != 0) {
        LOG_CMD_CTX(ctx, LOG_WARN, "ssconfig: failed to read BypassUID from %s",
                    g_cfg.cloak_ck_config);
        return reply_error(ctx,
            "Failed to read Cloak UID\n"
            "Check CLOAK_CK_CONFIG path");
    }

    /* RedirAddr has a sensible fallback if missing */
    if (json_read_string(g_cfg.cloak_ck_config, "RedirAddr",
                          ck_redir_addr, sizeof(ck_redir_addr)) != 0) {
        safe_copy(ck_redir_addr, sizeof(ck_redir_addr), "www.bing.com");
    }

    LOG_CMD_CTX(ctx, LOG_DEBUG,
        "ssconfig: port=%d method=%s uid=%.8s... redir=%s",
        ss_port, ss_method, ck_uid, ck_redir_addr);

    /* ------------------------------------------------------------------ */
    /* 4. Build timestamped file paths                                     */
    /* ------------------------------------------------------------------ */
    time_t ts = time(NULL);

    char ck_path[SSCONFIG_PATH_MAX];
    char ss_path[SSCONFIG_PATH_MAX];

    snprintf(ck_path, sizeof(ck_path), "%s/cloak-client-%ld.json",
             g_cfg.upload_dir, (long)ts);
    snprintf(ss_path, sizeof(ss_path), "%s/ss-client-%ld.json",
             g_cfg.upload_dir, (long)ts);

    /* ------------------------------------------------------------------ */
    /* 5. Write cloak-client-<ts>.json (atomic)                           */
    /* ------------------------------------------------------------------ */
    cJSON *ck_json = cJSON_CreateObject();
    if (!ck_json) return reply_error(ctx, "Out of memory");

    cJSON_AddStringToObject(ck_json, "Transport",        "direct");
    cJSON_AddStringToObject(ck_json, "ProxyMethod",      "shadowsocks");
    cJSON_AddStringToObject(ck_json, "EncryptionMethod", "plain");
    cJSON_AddStringToObject(ck_json, "UID",              ck_uid);
    cJSON_AddStringToObject(ck_json, "PublicKey",        g_cfg.cloak_public_key);
    cJSON_AddStringToObject(ck_json, "ServerName",       ck_redir_addr);
    cJSON_AddNumberToObject(ck_json, "NumConn",          4);
    cJSON_AddStringToObject(ck_json, "BrowserSig",       "chrome");
    cJSON_AddNumberToObject(ck_json, "StreamTimeout",    300);

    if (write_json_file_atomic(ck_path, ck_json) != 0) {
        cJSON_Delete(ck_json);
        LOG_CMD_CTX(ctx, LOG_WARN, "ssconfig: failed to write %s", ck_path);
        return reply_error(ctx,
            "Failed to write cloak-client.json\n"
            "Check UPLOAD_DIR permissions");
    }
    cJSON_Delete(ck_json);

    /* ------------------------------------------------------------------ */
    /* 6. Write ss-client-<ts>.json (atomic)                              */
    /* ------------------------------------------------------------------ */

    /*
     * plugin_opts points to the cloak config filename only (no path).
     * The SS client looks for it relative to its working directory,
     * so both files should be placed in the same directory.
     */
    char ck_filename[256];
    snprintf(ck_filename, sizeof(ck_filename), "cloak-client-%ld.json", (long)ts);

    cJSON *ss_json = cJSON_CreateObject();
    if (!ss_json) {
        remove(ck_path);
        return reply_error(ctx, "Out of memory");
    }

    cJSON_AddStringToObject(ss_json, "server",      server_ip);
    cJSON_AddNumberToObject(ss_json, "server_port", ss_port);
    cJSON_AddNumberToObject(ss_json, "local_port",  1080);
    cJSON_AddStringToObject(ss_json, "password",    ss_password);
    cJSON_AddStringToObject(ss_json, "method",      ss_method);
    cJSON_AddStringToObject(ss_json, "plugin",      "ck-client");
    cJSON_AddStringToObject(ss_json, "plugin_opts", ck_filename);

    if (write_json_file_atomic(ss_path, ss_json) != 0) {
        cJSON_Delete(ss_json);
        remove(ck_path);  /* clean up first file on failure */
        LOG_CMD_CTX(ctx, LOG_WARN, "ssconfig: failed to write %s", ss_path);
        return reply_error(ctx,
            "Failed to write ss-client.json\n"
            "Check UPLOAD_DIR permissions");
    }
    cJSON_Delete(ss_json);

    LOG_CMD_CTX(ctx, LOG_INFO,
        "ssconfig: generated cloak-client-%ld.json and ss-client-%ld.json",
        (long)ts, (long)ts);
    METRICS_CMD(ssconfig);

    /* ------------------------------------------------------------------ */
    /* 7. Reply with summary                                               */
    /* ------------------------------------------------------------------ */
    char reply[RESP_MAX];
    snprintf(reply, sizeof(reply),
        "⚙️ Shadowsocks + Cloak Config\n\n"
        "Server:  %s:%d\n"
        "Method:  %s\n"
        "Cloak:   %s\n\n"
        "Files saved to:\n"
        "%s\n"
        "%s\n\n"
        "Download:\n"
        "scp user@%s:%s .\n"
        "scp user@%s:%s .\n\n"
        "Delete after use:\n"
        "rm %s\n"
        "rm %s",
        server_ip, ss_port,
        ss_method,
        ck_redir_addr,
        ck_path,
        ss_path,
        server_ip, ck_path,
        server_ip, ss_path,
        ck_path,
        ss_path
    );

    return reply_plain(ctx, reply);
}
