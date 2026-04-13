#ifndef SECURITY_H
#define SECURITY_H

// init
void security_set_allowed_chat(long chat_id);
void security_set_token_ttl(int ttl);

// доступ
int security_is_allowed_chat(long chat_id);
int security_validate_text(const char *text);

// reboot tokens
int security_generate_reboot_token(long chat_id);
int security_validate_reboot_token(long chat_id, int token);

#endif
