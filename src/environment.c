/**
 * tg-bot - Telegram bot for system administration
 * 
 * environment.c - Runtime environment diagnostics and validation
 * 
 * Performs startup checks:
 *   - CI environment detection
 *   - User/group context logging (UID, EUID)
 *   - Working directory logging
 *   - Access checks for system utilities:
 *       - journalctl (via sudo)
 *       - systemctl (via sudo)
 *       - fail2ban-wrapper (via sudo)
 *   - Log file write access verification
 * 
 * These checks help diagnose permission issues and missing dependencies
 * early in the startup process, before entering the main event loop.
 * 
 * MIT License
 * 
 * Copyright (c) 2026 ironmist45
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

#include "environment.h"
#include "logger.h"
#include "exec.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

// ===== CI detection =====

int env_is_ci(void) {
    return getenv("CI") != NULL;
}

// ===== Context logging =====

void env_log_user_info(void) {
    uid_t uid = getuid();
    uid_t euid = geteuid();

    struct passwd *pw = getpwuid(uid);
    struct passwd *epw = getpwuid(euid);

    LOG_SYS(LOG_INFO, "User UID: %d (%s)", 
            uid, pw ? pw->pw_name : "unknown");
    LOG_SYS(LOG_INFO, "Effective UID: %d (%s)", 
            euid, epw ? epw->pw_name : "unknown");
}

void env_log_workdir(void) {
    char buf[256];
    if (getcwd(buf, sizeof(buf))) {
        LOG_SYS(LOG_INFO, "Working directory: %s", buf);
    } else {
        LOG_SYS(LOG_WARN, "Cannot determine working directory");
    }
}

// ===== Access checks =====

void env_check_journal_access(void) {
    char *argv[] = {
        (char *)g_cfg.sudo_path, "-n",
        (char *)g_cfg.journalctl_path,
        "-n", "1", "--no-pager",
        NULL
    };

    char output[512];
    exec_result_t res;

    exec_opts_t opts = {
        .timeout_ms = 5000,
        .capture_stderr = 1,
        .log_output = 0,
        .quiet = 1
    };

    int rc = exec_command(argv, output, sizeof(output), &opts, &res);

    if (rc != 0) {
        LOG_SYS(LOG_WARN, "journalctl: execution failed (%s)",
                exec_status_str(res.status));
        return;
    }

    if (res.exit_code != 0) {
        LOG_SYS(LOG_WARN, "journalctl: no access via sudo (exit=%d)",
                res.exit_code);
        return;
    }

    LOG_SYS(LOG_INFO, "journalctl: access OK (via sudo)");
}

void env_check_systemctl_access(void) {
    char *const args[] = {
        (char *)g_cfg.sudo_path, "-n",
        (char *)g_cfg.systemctl_path,
        "is-active", "ssh", NULL
    };

    char out[128] = {0};
    exec_result_t res;

    exec_opts_t opts = {
        .timeout_ms = 2000,
        .quiet = 1
    };

    if (!exec_check_cmd(args, out, sizeof(out), &opts, &res)) {
        if (strstr(out, "command not found")) {
            LOG_SYS(LOG_ERROR, "systemctl: FAIL (missing)");
            return;
        }
        
        if (strstr(out, "password is required") || strstr(out, "no tty")) {
            LOG_SYS(LOG_ERROR, "systemctl: FAIL (sudo)");
            return;
        }

        if (res.status == EXEC_EXEC_FAILED) {
            LOG_SYS(LOG_ERROR, "systemctl: FAIL (exec)");
            return;
        }

        if (out[0] == '\0') {
            LOG_SYS(LOG_ERROR, "systemctl: FAIL (no output, %s)",
                    exec_status_str(res.status));
            return;
        }

        LOG_SYS(LOG_ERROR, "systemctl: FAIL (%s)", exec_status_str(res.status));
        return;
    }

    LOG_SYS(LOG_INFO, "systemctl: OK");
}

void env_check_fail2ban(void) {
    if (env_is_ci()) {
        LOG_SYS(LOG_INFO, "fail2ban-wrapper: skipped (CI environment)");
        return;
    }
    
    char *const args[] = {
        (char *)g_cfg.sudo_path, "-n",
        (char *)g_cfg.f2b_wrapper_path,
        "status",
        NULL
    };

    char out[256] = {0};
    exec_result_t res;

    exec_opts_t opts = {
        .timeout_ms = 5000,
        .quiet = 1
    };

    if (!exec_check_cmd(args, out, sizeof(out), &opts, &res)) {
        if (strstr(out, "No such file")) {
            LOG_SYS(LOG_ERROR, "fail2ban-wrapper: FAIL (missing)");
        } else {
            LOG_SYS(LOG_ERROR, "fail2ban-wrapper: FAIL (%s)", 
                    exec_status_str(res.status));
        }
        return;
    }
        
    LOG_SYS(LOG_INFO, "fail2ban-wrapper: OK");
}

void env_check_logfile_access(const char *path) {
    if (!path) {
        LOG_SYS(LOG_WARN, "No logfile path provided for access check");
        return;
    }
    
    FILE *f = fopen(path, "a");
    if (!f) {
        LOG_SYS(LOG_WARN, "No write access: %s", path);
        return;
    }
    fclose(f);
}

void env_check_all(const char *log_path) {
    LOG_SYS(LOG_INFO, "Running environment checks...");
    
    env_check_journal_access();
    env_check_systemctl_access();
    env_check_fail2ban();
    env_check_logfile_access(log_path);
    
    LOG_SYS(LOG_INFO, "Environment checks completed");
}
