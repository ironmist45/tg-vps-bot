#ifndef SECURITY_H
#define SECURITY_H

int security_generate_reboot_token(long chat_id);
int security_validate_reboot_token(long chat_id, int token);

// уже было
int security_is_allowed_chat(long chat_id);
int security_validate_text(const char *text);

#endif
