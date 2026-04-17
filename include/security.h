#ifndef SECURITY_H
#define SECURITY_H

#include <stddef.h>

// ===== INIT =====

void security_init(void);

// ===== CONFIG =====

void security_set_allowed_chat(long chat_id);
void security_set_token_ttl(int ttl);

// ===== ACCESS CONTROL =====

int security_is_allowed_chat(long chat_id);
int security_check_access(long chat_id, const char *cmd);
int security_validate_text(const char *text);

// ===== REBOOT TOKENS =====
//
// Stateless токены (без хранения в памяти)
//

int security_generate_reboot_token(long chat_id);
int security_validate_reboot_token(long chat_id, int token);

#endif // SECURITY_H
