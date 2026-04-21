/**
 * tg-bot - Telegram bot for system administration
 * version.h - Application version and build information
 * MIT License - Copyright (c) 2026
 */

#ifndef VERSION_H
#define VERSION_H

#include <time.h>

// ============================================================================
// BASIC VERSION INFORMATION
// ============================================================================

#define APP_NAME    "tg-bot"                 // Application name
#define APP_VERSION "1.2.6"                  // Semantic version
#define TARGET_OS   "Ubuntu 18.04.6 LTS x86_64"  // Target platform

// ============================================================================
// META INFORMATION
// ============================================================================

#define APP_CODENAME "Orion"     // Release codename
#define APP_YEAR     "2026"      // Copyright year
#define APP_AUTHOR   "dedoverde" // Author

// ============================================================================
// BUILD INFORMATION (AUTOMATIC)
// ============================================================================

#define BUILD_DATE __DATE__  // Compilation date (e.g., "Apr 21 2026")
#define BUILD_TIME __TIME__  // Compilation time (e.g., "15:04:05")

// ============================================================================
// RUNTIME INFORMATION
// ============================================================================

/**
 * Bot process start time
 * 
 * Initialized in lifecycle.c at program startup.
 * Used for uptime calculation in /about and /ping commands.
 */
extern time_t g_start_time;

#endif // VERSION_H
