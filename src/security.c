#include "security.h"
#include "logger.h"

#include <string.h>
#include <ctype.h>

// ===== глобальные настройки =====

static long g_allowed_chat_id = 0;

// ===== init =====

void security_init(long allowed_chat_id) {
    g_allowed_chat_id = allowed_chat_id;
}

// ===== проверка chat_id =====

int security_is_allowed_chat(long chat_id) {
    if (chat_id != g_allowed_chat_id) {
        log_msg(LOG_WARN, "Access denied for chat_id=%ld", chat_id);
        return 0;
    }
    return 1;
}

// ===== базовая фильтрация текста =====

int security_validate_text(const char *text) {

    if (!text || text[0] == '\0') {
        return -1;
    }

    // ограничение длины (защита от мусора)
    size_t len = strlen(text);
    if (len > 1024) {
        log_msg(LOG_WARN, "Message too long (%zu)", len);
        return -1;
    }

    // запрет бинарных/непечатных символов
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];

        if (c < 32 && c != '\n' && c != '\r' && c != '\t') {
            log_msg(LOG_WARN, "Invalid char detected");
            return -1;
        }
    }

    return 0;
}
