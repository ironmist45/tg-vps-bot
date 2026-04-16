#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// ===== безопасная проверка IP =====

static int is_safe_ip(const char *ip) {
    if (!ip || !*ip)
        return 0;

    int dots = 0;
    int num = 0;
    int has_digit = 0;

    for (size_t i = 0; ip[i]; i++) {

        if (isdigit((unsigned char)ip[i])) {
            num = num * 10 + (ip[i] - '0');

            if (num > 255)
                return 0;

            has_digit = 1;
        }
        else if (ip[i] == '.') {

            if (!has_digit)
                return 0;

            dots++;
            num = 0;
            has_digit = 0;
        }
        else {
            return 0;
        }
    }

    return (dots == 3 && has_digit);
}

// ===== main =====

int main(int argc, char *argv[]) {

    if (argc < 2) {
        fprintf(stderr, "Usage: f2b-wrapper <command>\n");
        return 1;
    }

    // ===== status =====
    if (strcmp(argv[1], "status") == 0) {

        if (argc == 2) {
            execlp("fail2ban-client",
                   "fail2ban-client",
                   "status",
                   NULL);
            perror("execlp: fail2ban-client exec failed");
            return 1;
        }

        if (argc == 3 && strcmp(argv[2], "sshd") == 0) {
            execlp("fail2ban-client",
                   "fail2ban-client",
                   "status",
                   "sshd",
                   NULL);
            perror("execlp: fail2ban-client exec failed");
            return 1;
        }

        fprintf(stderr, "Invalid status command\n");
        return 1;
    }

    // ===== set sshd ban/unban =====
    if (strcmp(argv[1], "set") == 0) {

        if (argc == 5 &&
            strcmp(argv[2], "sshd") == 0 &&
            (strcmp(argv[3], "banip") == 0 ||
             strcmp(argv[3], "unbanip") == 0) &&
            is_safe_ip(argv[4])) {

            execlp("fail2ban-client",
                   "fail2ban-client",
                   "set",
                   "sshd",
                   argv[3],
                   argv[4],
                   NULL);
            perror("execlp: fail2ban-client exec failed");
            return 1;
        }

        fprintf(stderr, "Invalid set command\n");
        return 1;
    }

    // ===== всё остальное запрещено =====
    fprintf(stderr, "Command not allowed\n");
    return 1;
}
