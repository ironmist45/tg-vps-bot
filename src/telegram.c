/**
 * tg-bot - Telegram bot for system administration
 * 
 * telegram.c - Telegram Bot API communication layer
 * 
 * Handles all interaction with the Telegram Bot API:
 *   - Long polling for incoming messages (getUpdates)
 *   - Sending messages with MarkdownV2 formatting
 *   - Sending plain text messages
 *   - Update offset persistence (crash recovery)
 *   - Request ID generation for log correlation
 *   - Message truncation for Telegram limits
 *   - MarkdownV2 character escaping
 * 
 * Dependencies: libcurl (HTTP), cJSON (JSON parsing)
 * 
 * MIT License
 * 
 * Copyright (c) 2026
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "telegram.h"
#include "logger.h"
#include "commands.h"
#include "security.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>

#include <curl/curl.h>
#include <cjson/cJSON.h>

// ============================================================================
// EXTERNAL GLOBAL VARIABLES (from lifecycle.c)
// ============================================================================

// Shutdown flag (0 = normal, 1 = restart, 2 = reboot, etc.)
extern volatile sig_atomic_t g_shutdown_requested;

// Signal received flag (set to 1 when SIGTERM/SIGINT is caught)
extern volatile sig_atomic_t g_signal_received;

// ============================================================================
// CONSTANTS
// ============================================================================

#define URL_MAX   1024   // Maximum URL length
#define RESP_MAX  8192   // Maximum response buffer size
#define TG_LIMIT  4096   // Telegram message length limit

// Offset persistence files
#define OFFSET_FILE "/var/lib/tg-bot/offset.dat"
#define OFFSET_TMP  "/var/lib/tg-bot/offset.tmp"

// ============================================================================
// GLOBAL STATE
// ============================================================================

// Telegram API credentials
static char g_token[128];
static char g_base_url[512];

// Runtime duplicate protection (in-memory)
static long g_last_processed_update = 0;

// Offset write throttling (save every N updates)
static int g_offset_counter = 0;

// Poll cycle counter for logging
static unsigned short g_poll_cycle = 0;
static unsigned short g_current_poll_id = 0;

/**
 * Get current poll cycle identifier
 * Used by other modules to correlate logs with polling cycles
 */
unsigned short telegram_get_poll_id(void) {
    return g_current_poll_id;
}

/**
 * Get last saved offset (for shutdown logging)
 */
long telegram_get_last_offset(void) {
    return g_last_saved_offset;
}

// Last saved offset (for shutdown logging)
static long g_last_saved_offset = 0;

// ============================================================================
// OFFSET PERSISTENCE (CRASH RECOVERY)
// ============================================================================

/**
 * Load last processed update_id from disk
 * 
 * Used to resume polling from the correct position after restart.
 * 
 * @return  Last processed update_id, or 0 if no saved state
 */
static long load_offset(void) {
    FILE *f = fopen(OFFSET_FILE, "r");
    if (!f) return 0;

    long offset = 0;
    if (fscanf(f, "%ld", &offset) != 1)
        offset = 0;

    fclose(f);

    LOG_STATE(LOG_INFO, "Loaded offset: %ld", offset);
    return offset;
}

/**
 * Save current update_id to disk (atomic via rename)
 * 
 * Uses temporary file + rename to ensure atomic writes.
 * 
 * @param offset  Update_id to save
 */
static void save_offset(long offset) {
    FILE *f = fopen(OFFSET_TMP, "w");
    if (!f) {
        LOG_STATE(LOG_WARN, "Failed to save offset (tmp)");
        return;
    }

    fprintf(f, "%ld\n", offset);
    fclose(f);

    if (rename(OFFSET_TMP, OFFSET_FILE) != 0) {
        LOG_STATE(LOG_WARN, "Failed to rename offset file");
    }
}

// ============================================================================
// CURL CALLBACKS
// ============================================================================

/**
 * Discard callback for curl (used when response body is not needed)
 */
static size_t discard_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)ptr;
    (void)userdata;
    return size * nmemb;
}

/**
 * Write callback for curl - accumulates response in memory buffer
 */
struct memory {
    char *data;
    size_t size;
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t real_size = size * nmemb;
    struct memory *mem = (struct memory *)userp;

    char *ptr = realloc(mem->data, mem->size + real_size + 1);
    if (!ptr) {
        LOG_NET(LOG_ERROR, "realloc failed");
        free(mem->data);
        mem->data = NULL;
        mem->size = 0;
        return 0;
    }

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, real_size);
    mem->size += real_size;
    mem->data[mem->size] = '\0';

    return real_size;
}

// ============================================================================
// MARKDOWN ESCAPING
// ============================================================================

/**
 * Escape special characters for Telegram MarkdownV2
 * 
 * Characters escaped: _ * [ ] ( ) ~ ` > # + - = | { } . !
 * 
 * @param src   Source string
 * @param dst   Destination buffer
 * @param size  Size of destination buffer
 */
static void escape_markdown(const char *src, char *dst, size_t size) {
    const char *special = "_[]()~>#+-=|{}.!";
    size_t j = 0;

    for (size_t i = 0; src[i] != '\0' && j + 2 < size; i++) {
        if (strchr(special, src[i])) {
            dst[j++] = '\\';
        }
        dst[j++] = src[i];
    }

    dst[j] = '\0';
}

// ============================================================================
// MESSAGE TRUNCATION
// ============================================================================

/**
 * Truncate message to Telegram's 4096 character limit
 * 
 * Adds "[truncated]" marker if truncation occurred.
 * 
 * @param text  Message text (modified in-place)
 */
static void truncate_message(char *text) {
    size_t len = strlen(text);

    if (len >= TG_LIMIT && TG_LIMIT > 20) {
        text[TG_LIMIT - 20] = '\0';
        snprintf(text + strlen(text),
                 TG_LIMIT - strlen(text),
                 "\n...\n[truncated]");
    }
}

// ============================================================================
// REQUEST ID GENERATION
// ============================================================================

/**
 * Generate 16-bit request ID from update_id for log correlation
 * 
 * Uses a simple non-cryptographic hash for distribution.
 * 
 * @param update_uid  Telegram update_id
 * @return            16-bit request identifier (0000-FFFF)
 */
static unsigned short make_req_id(long update_uid) {
    uint32_t x = (uint32_t)update_uid;

    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;

    return (unsigned short)(x & 0xFFFF);
}

// ============================================================================
// CURL CONFIGURATION
// ============================================================================

/**
 * Configure curl handle with standard timeouts and keepalive
 * 
 * @param curl  Curl handle to configure
 */
static void setup_curl(CURL *curl) {
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 35L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);
}

// ============================================================================
// INITIALIZATION AND SHUTDOWN
// ============================================================================

/**
 * Initialize Telegram module
 * 
 * @param token  Telegram Bot API token
 * @return       0 on success, -1 on error
 */
int telegram_init(const char *token) {
    if (!token || strlen(token) == 0)
        return -1;

    strncpy(g_token, token, sizeof(g_token) - 1);
    g_token[sizeof(g_token) - 1] = '\0';

    snprintf(g_base_url, sizeof(g_base_url),
             "https://api.telegram.org/bot%s", g_token);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    LOG_NET(LOG_INFO, "Telegram initialized");
    return 0;
}

/**
 * Shutdown Telegram module and cleanup curl
 */
void telegram_shutdown(void) {
    curl_global_cleanup();
    LOG_NET(LOG_INFO, "Telegram shutdown");
}

// ============================================================================
// SENDING MESSAGES
// ============================================================================

/**
 * Send MarkdownV2 formatted message
 * 
 * Automatically escapes special characters and truncates if needed.
 * 
 * @param chat_id  Target chat ID
 * @param text     Message text (may contain MarkdownV2)
 * @return         0 on success, -1 on error
 */
int telegram_send_message(long chat_id, const char *text) {
    if (!text) return -1;
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    setup_curl(curl);

    char url[URL_MAX];
    snprintf(url, sizeof(url), "%s/sendMessage", g_base_url);
    LOG_NET(LOG_DEBUG, "send message request");
    LOG_NET(LOG_DEBUG, "send url: %s", url);

    char tmp[RESP_MAX];
    strncpy(tmp, text, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    truncate_message(tmp);

    char escaped[RESP_MAX];
    escape_markdown(tmp, escaped, sizeof(escaped));

    char post_fields[RESP_MAX];
    snprintf(post_fields, sizeof(post_fields),
             "chat_id=%ld&text=%s&parse_mode=MarkdownV2",
             chat_id, escaped);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_callback);
      
    LOG_NET(LOG_DEBUG, "send md: chat_id=%ld len=%zu", chat_id, strlen(text));
    
    CURLcode res = curl_easy_perform(curl);
    
    LOG_NET(LOG_DEBUG, "send curl result: %d (%s)", res, curl_easy_strerror(res));

    if (res != CURLE_OK) {
        LOG_NET(LOG_ERROR, "curl send error: %s", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    return 0;
}

/**
 * Send plain text message (no Markdown parsing)
 * 
 * @param chat_id  Target chat ID
 * @param text     Plain text message
 * @return         0 on success, -1 on error
 */
int telegram_send_plain(long chat_id, const char *text) {
    if (!text) return -1;
    
    LOG_NET(LOG_DEBUG, "send plain: chat_id=%ld len=%zu", chat_id, strlen(text));
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    setup_curl(curl);

    char url[URL_MAX];
    snprintf(url, sizeof(url), "%s/sendMessage", g_base_url);

    char tmp[RESP_MAX];
    strncpy(tmp, text, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    truncate_message(tmp);

    char post_fields[RESP_MAX];
    snprintf(post_fields, sizeof(post_fields),
             "chat_id=%ld&text=%s",
             chat_id, tmp);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_callback);

    CURLcode res = curl_easy_perform(curl);

    LOG_NET(LOG_DEBUG, "send plain curl result: %d (%s)", res, curl_easy_strerror(res));

    if (res != CURLE_OK) {
        LOG_NET(LOG_ERROR, "curl send error: %s", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
    return 0;
}

// ============================================================================
// LONG POLLING (Fork + Alarm Strategy with Full Signal Debugging)
// ============================================================================

/**
 * Poll Telegram API for new messages (non-blocking with shutdown check)
 * 
 * Uses a child process to isolate the blocking curl_easy_perform call.
 * This guarantees that the main process can respond to SIGTERM immediately
 * and never hangs indefinitely on network issues.
 * 
 * @return  0 on success, -1 on error or aborted by shutdown
 */
int telegram_poll() {
    static long last_update_id = -1;

    // Increment poll cycle counter
    g_poll_cycle++;
    unsigned short poll_id = g_poll_cycle;
    g_current_poll_id = poll_id;

    // Load saved offset on first call
    if (last_update_id == -1) {
        last_update_id = load_offset();
        LOG_NET(LOG_INFO, "Starting poll from offset: %ld", last_update_id);
    }

    // Prepare URL for the child process
    char url[URL_MAX];
    snprintf(url, sizeof(url),
             "%s/getUpdates?timeout=25&offset=%ld",
             g_base_url, last_update_id);

    LOG_NET(LOG_DEBUG, "poll=%04x request (offset=%ld)", poll_id, last_update_id);

    // Create pipe for IPC (child -> parent)
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        LOG_NET(LOG_ERROR, "poll=%04x pipe failed", poll_id);
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        LOG_NET(LOG_ERROR, "poll=%04x fork failed", poll_id);
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    // ===== CHILD PROCESS (executes the dangerous network call) =====
    if (pid == 0) {
        close(pipefd[0]); // Child does not read from pipe
        
        CURL *curl = curl_easy_init();
        if (!curl) {
            _exit(1);
        }

        setup_curl(curl);

        struct memory chunk = {0};

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);

        CURLcode res = curl_easy_perform(curl);
        
        // Send result back to parent via pipe
        FILE *pipe_write = fdopen(pipefd[1], "w");
        if (pipe_write) {
            // Protocol: [RESULT_CODE]\n[DATA_SIZE]\n[DATA]
            fprintf(pipe_write, "%d\n", res);
            if (chunk.data) {
                fprintf(pipe_write, "%zu\n", chunk.size);
                fwrite(chunk.data, 1, chunk.size, pipe_write);
            } else {
                fprintf(pipe_write, "0\n");
            }
            fflush(pipe_write);
            fclose(pipe_write);
        }

        curl_easy_cleanup(curl);
        free(chunk.data);
        _exit(0);
    }

    // ===== PARENT PROCESS (main bot, monitors the child) =====
    close(pipefd[1]); // Parent does not write to pipe

    char chunk_data[RESP_MAX];
    memset(chunk_data, 0, sizeof(chunk_data));
    CURLcode res = CURLE_OK;
    int data_received = 0;
    
    // Set a 35-second timeout for the child
    time_t start_wait = time(NULL);
    int wait_timeout = 35;

    LOG_NET(LOG_DEBUG, "poll=%04x parent waiting for child (PID=%d)", poll_id, pid);

    while (1) {
        // Check if child has exited
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        
        if (result == pid) {
            // Child finished!
            LOG_NET(LOG_DEBUG, "poll=%04x child process exited", poll_id);
            if (WIFEXITED(status)) {
                // Read data from pipe
                if (!data_received) {
                    FILE *pipe_read = fdopen(pipefd[0], "r");
                    if (pipe_read) {
                        int res_code;
                        size_t data_len;
                        if (fscanf(pipe_read, "%d\n%zu\n", &res_code, &data_len) == 2) {
                            res = (CURLcode)res_code;
                            if (data_len > 0 && data_len < RESP_MAX) {
                                fread(chunk_data, 1, data_len, pipe_read);
                            }
                        }
                        fclose(pipe_read);
                        data_received = 1;
                    }
                }
            }
            break; // Exit wait loop
        }

        // 🔥 Check BOTH global flags from lifecycle.c
        if (g_signal_received != 0 || g_shutdown_requested != 0) {
            LOG_NET(LOG_WARN, "poll=%04x aborting child (shutdown=%d, signal=%d)", 
                    poll_id, g_shutdown_requested, g_signal_received);
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            close(pipefd[0]);
            return -1;
        }

        // Check our own timeout (35 seconds)
        if (time(NULL) - start_wait > wait_timeout) {
            LOG_NET(LOG_ERROR, "poll=%04x child process timed out after %d seconds. Killing.", poll_id, wait_timeout);
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            close(pipefd[0]);
            return -1;
        }

        // Sleep 100ms but allow signal interruption
        int poll_rc = poll(NULL, 0, 100);
        if (poll_rc == -1 && errno == EINTR) {
            LOG_NET(LOG_DEBUG, "poll=%04x interrupted by signal", poll_id);
        }
        
        // Debug log showing BOTH flags
        LOG_NET(LOG_DEBUG, "poll=%04x loop: shutdown=%d, signal=%d, child_alive=%d", 
                poll_id, g_shutdown_requested, g_signal_received, (kill(pid, 0) == 0));
    }

    close(pipefd[0]); // Close read end of pipe

    // ===== PROCESS RESULT (same as before) =====
    LOG_NET(LOG_DEBUG,
            "poll=%04x curl result: %d (%s)",
            poll_id, res,
            curl_easy_strerror(res));

    // Timeout is normal - just means no new messages
    if (res == CURLE_OPERATION_TIMEDOUT) {
        return 0;
    }

    if (res != CURLE_OK) {
        LOG_NET(LOG_ERROR, "poll=%04x curl error: %s", poll_id, curl_easy_strerror(res));
        return -1;
    }

    if (strlen(chunk_data) == 0) {
        LOG_NET(LOG_DEBUG, "poll=%04x empty response from Telegram", poll_id);
        return 0;
    }

    // Parse JSON response
    cJSON *json = cJSON_Parse(chunk_data);
    if (!json) {
        LOG_NET(LOG_ERROR, "poll=%04x JSON parse error", poll_id);
        return -1;
    }

    cJSON *ok = cJSON_GetObjectItem(json, "ok");
    if (!cJSON_IsTrue(ok)) {
        cJSON *error_code = cJSON_GetObjectItem(json, "error_code");
        cJSON *description = cJSON_GetObjectItem(json, "description");
        LOG_NET(LOG_ERROR,
                "poll=%04x API error %d: %s",
                poll_id,
                error_code ? error_code->valueint : 0,
                description ? description->valuestring : "unknown");
        cJSON_Delete(json);
        return -1;
    }

    cJSON *result = cJSON_GetObjectItem(json, "result");

    // Process each update
    if (cJSON_IsArray(result)) {
        int count = cJSON_GetArraySize(result);
        LOG_NET(LOG_DEBUG, "updates received: %d", count);

        for (int i = 0; i < count; i++) {
            cJSON *update = cJSON_GetArrayItem(result, i);

            cJSON *update_id = cJSON_GetObjectItem(update, "update_id");
            if (!update_id) {
                LOG_NET(LOG_WARN, "update without update_id");
                continue;
            }

            long update_uid = (long)update_id->valuedouble;
            unsigned short req_id = make_req_id(update_uid);
            LOG_NET(LOG_DEBUG, "update_id=%ld", update_uid);

            // Skip already processed updates
            if (update_uid <= g_last_processed_update)
                continue;

            g_last_processed_update = update_uid;
            last_update_id = update_uid + 1;
            g_last_saved_offset = last_update_id;

            // Throttle offset writes (every 5 updates)
            if (++g_offset_counter >= 5) {
                LOG_NET(LOG_DEBUG, "saving offset: %ld", last_update_id);
                save_offset(last_update_id);
                g_offset_counter = 0;
            }

            cJSON *message = cJSON_GetObjectItem(update, "message");
            if (!message) {
                LOG_NET(LOG_DEBUG, "skipping update: no message");
                continue;
            }

            // Extract user info
            cJSON *from = cJSON_GetObjectItem(message, "from");
            cJSON *user_id_json = from ? cJSON_GetObjectItem(from, "id") : NULL;
            cJSON *username_json = from ? cJSON_GetObjectItem(from, "username") : NULL;

            int uid = 0;
            const char *uname = NULL;

            if (user_id_json) {
                uid = (int)user_id_json->valuedouble;
            }
            if (username_json && cJSON_IsString(username_json)) {
                uname = username_json->valuestring;
            }

            // Extract message date
            cJSON *date = cJSON_GetObjectItem(message, "date");
            time_t msg_date = 0;
            if (date && cJSON_IsNumber(date)) {
                msg_date = (time_t)date->valuedouble;
            }

            // Extract chat and text
            cJSON *chat = cJSON_GetObjectItem(message, "chat");
            if (!chat) {
                LOG_NET(LOG_DEBUG, "skipping update: no chat");
                continue;
            }

            cJSON *chat_id = cJSON_GetObjectItem(chat, "id");
            cJSON *text = cJSON_GetObjectItem(message, "text");

            if (!chat_id || !text || !cJSON_IsString(text)) {
                LOG_NET(LOG_DEBUG, "skipping update: no text");
                continue;
            }

            long cid = (long)chat_id->valuedouble;
            const char *msg_text = text->valuestring;

            LOG_NET(LOG_INFO,
                "poll=%04x req=%04x upd=%ld incoming: chat_id=%ld len=%zu text=%.64s",
                g_current_poll_id, req_id, update_uid, cid, strlen(msg_text), msg_text);
            LOG_NET(LOG_DEBUG, "user: id=%d username=%s", uid, uname ? uname : "NULL");

            // Security checks
            if (!security_is_allowed_chat(cid)) {
                LOG_NET(LOG_WARN,
                    "poll=%04x req=%04x ACCESS CHECK: chat_id=%ld cmd=%s result=DENIED",
                    g_current_poll_id, req_id, cid, msg_text);
                continue;
            }

            if (security_validate_text(msg_text) != 0)
                continue;

            // Process command
            struct timespec req_start, req_end;
            clock_gettime(CLOCK_MONOTONIC, &req_start);
            char response[RESP_MAX];
            response_type_t resp_type;

            LOG_NET(LOG_DEBUG, "req=%04x dispatch → commands_handle", req_id);

            if (commands_handle(msg_text, cid, msg_date,
                                uid, uname, req_id,
                                response, sizeof(response), &resp_type) == 0) {
                
                LOG_NET(LOG_DEBUG, "req=%04x response ready (%zu bytes)", req_id, strlen(response));

                // Use plain text for commands with raw output
                if (strncmp(msg_text, "/logs", 5) == 0 ||
                    strncmp(msg_text, "/fail2ban", 9) == 0) {
                    telegram_send_plain(cid, response);
                } else {
                    telegram_send_message(cid, response);
                }

                clock_gettime(CLOCK_MONOTONIC, &req_end);
                long req_ms = (req_end.tv_sec - req_start.tv_sec) * 1000 +
                              (req_end.tv_nsec - req_start.tv_nsec) / 1000000;

                LOG_NET(LOG_INFO, "poll=%04x req=%04x done: %ld ms (resp=%zu)",
                        g_current_poll_id, req_id, req_ms, strlen(response));
            }
        }
    }

    cJSON_Delete(json);
    return 0;
}
