/**
 * tg-bot - Telegram bot for system administration
 * telegram_offset.c - Offset persistence implementation
 * MIT License - Copyright (c) 2026
 */

#include "telegram_offset.h"
#include "logger.h"

#include <stdio.h>
#include <unistd.h>

#define OFFSET_FILE "/var/lib/tg-bot/offset.dat"
#define OFFSET_TMP  "/var/lib/tg-bot/offset.tmp"

// Last saved offset (for shutdown logging)
static long g_last_saved_offset = 0;

long telegram_offset_load(void) {
    FILE *f = fopen(OFFSET_FILE, "r");
    if (!f) return 0;

    long offset = 0;
    if (fscanf(f, "%ld", &offset) != 1)
        offset = 0;

    fclose(f);

    LOG_STATE(LOG_INFO, "Loaded offset: %ld", offset);
    return offset;
}

void telegram_offset_save(long offset) {
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
    
    g_last_saved_offset = offset;
}

long telegram_offset_get_last(void) {
    return g_last_saved_offset;
}
