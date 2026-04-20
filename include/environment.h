/**
 * tg-bot - Telegram bot for system administration
 * 
 * environment.h - Runtime environment diagnostics interface
 * 
 * Public API for:
 *   - CI environment detection
 *   - User context and working directory logging
 *   - System utility access validation (journalctl, systemctl, fail2ban)
 *   - Log file write permission verification
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

#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include <sys/types.h>

// ===== CI Detection =====

/**
 * Check if running in CI environment
 * 
 * @return 1 if CI environment variable is set, 0 otherwise
 */
int env_is_ci(void);

// ===== Context Logging =====

/**
 * Log current user and effective user information
 * Output format: "User UID: <uid> (<username>)"
 *                "Effective UID: <euid> (<username>)"
 */
void env_log_user_info(void);

/**
 * Log current working directory
 * Output format: "Working directory: <path>"
 */
void env_log_workdir(void);

// ===== Access Checks =====

/**
 * Check access to journalctl via sudo
 * Logs success or failure reason
 */
void env_check_journal_access(void);

/**
 * Check access to systemctl via sudo
 * Tests with "systemctl is-active ssh" command
 * Logs success or failure reason
 */
void env_check_systemctl_access(void);

/**
 * Check access to fail2ban-wrapper via sudo
 * Tests with "/usr/local/bin/f2b-wrapper status"
 * Logs success or failure reason (including GLIBC mismatch detection)
 */
void env_check_fail2ban(void);

/**
 * Check write access to log file
 * Attempts to open the specified file in append mode
 * 
 * @param path  path to log file to check
 */
void env_check_logfile_access(const char *path);

// ===== Comprehensive Check =====

/**
 * Run all environment checks at once
 * Convenience function that calls all individual check functions
 * 
 * @param log_path  path to log file for write access check
 */
void env_check_all(const char *log_path);

#endif // ENVIRONMENT_H
