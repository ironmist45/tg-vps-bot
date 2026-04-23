/**
 * tg-bot - Telegram bot for system administration
 * telegram_http.c - Low-level HTTP communication implementation
 * MIT License - Copyright (c) 2026
 */

#include "telegram_http.h"
#include "logger.h"
#include "lifecycle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <curl/curl.h>

#define URL_MAX 1024

static char g_base_url[512];

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

static void setup_curl(CURL *curl) {
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 35L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);
}

int telegram_http_init(const char *token) {
    if (!token || strlen(token) == 0) return -1;
    snprintf(g_base_url, sizeof(g_base_url), "https://api.telegram.org/bot%s", token);
    LOG_NET(LOG_INFO, "Telegram HTTP module initialized");
    return 0;
}

void telegram_http_shutdown(void) {
    LOG_NET(LOG_INFO, "Telegram HTTP module shutdown");
}

int telegram_http_request(const char *method, const char *post_fields, int need_response,
                          char **out_data, size_t *out_size) {
    LOG_NET(LOG_INFO, "telegram_http_request: ENTRY, method=%s", method);
    
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        LOG_NET(LOG_ERROR, "pipe failed");
        return -1;
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        LOG_NET(LOG_ERROR, "fork failed");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    
    if (pid == 0) {
        // Дочерний процесс
        close(pipefd[0]);
        
        CURL *curl = curl_easy_init();
        if (!curl) _exit(1);
        
        setup_curl(curl);
        
        char url[URL_MAX];
        snprintf(url, sizeof(url), "%s/%s", g_base_url, method);
        
        struct memory chunk = {0};
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
        
        CURLcode res = curl_easy_perform(curl);
        
        FILE *pipe_write = fdopen(pipefd[1], "w");
        if (pipe_write) {
            fprintf(pipe_write, "%d\n", res);
            if (chunk.data && need_response) {
                fprintf(pipe_write, "%zu\n", chunk.size);
                fwrite(chunk.data, 1, chunk.size, pipe_write);
            } else {
                fprintf(pipe_write, "0\n");
            }
            fclose(pipe_write);
        }
        
        curl_easy_cleanup(curl);
        if (chunk.data) free(chunk.data);
        _exit(0);
    }
    
    // Родительский процесс
    close(pipefd[1]);
    
    CURLcode res = CURLE_OK;
    char *data = NULL;
    size_t data_len = 0;
    
    time_t start = time(NULL);
    while (1) {
        if (lifecycle_shutdown_requested()) {
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            close(pipefd[0]);
            return -1;
        }
        
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            FILE *pipe_read = fdopen(pipefd[0], "r");
            if (pipe_read) {
                int res_code;
                size_t len;
                if (fscanf(pipe_read, "%d\n%zu\n", &res_code, &len) == 2) {
                    res = (CURLcode)res_code;
                    if (len > 0 && need_response) {
                        data = malloc(len + 1);
                        if (data) {
                            fread(data, 1, len, pipe_read);
                            data[len] = '\0';
                            data_len = len;
                        }
                    }
                }
                fclose(pipe_read);
            }
            break;
        }
        
        if (time(NULL) - start > 35) {
            LOG_NET(LOG_ERROR, "HTTP request timed out");
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            close(pipefd[0]);
            return -1;
        }
        
        usleep(100000);
    }
    
    close(pipefd[0]);
    
    if (res != CURLE_OK) {
        LOG_NET(LOG_ERROR, "curl error on %s: %s", method, curl_easy_strerror(res));
        if (data) free(data);
        return -1;
    }
    
    if (need_response) {
        *out_data = data;
        *out_size = data_len;
        LOG_NET(LOG_DEBUG, "telegram_http_request: returning data, size=%zu", data_len);
    } else {
        LOG_NET(LOG_DEBUG, "telegram_http_request: discarding data, size=%zu", data_len);
        if (data) free(data);
        *out_data = NULL;
        *out_size = 0;
    }
    
    LOG_NET(LOG_DEBUG, "telegram_http_request: success");
    return 0;
}
