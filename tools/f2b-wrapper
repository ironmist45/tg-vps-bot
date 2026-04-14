#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// ===== безопасная проверка IP =====

static int is_safe_ip(const char *ip) {
    if (!ip || !*ip)
        return 0;

    for (size_t i = 0; ip[i]; i++) {
        if (!(isdigit((unsigned char)ip[i]) || ip[i] == '.'))
            return 0;
    }

    return 1;
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
        }

        if (argc == 3 && strcmp(argv[2], "sshd") == 0) {
            execlp("fail2ban-client",
                   "fail2ban-client",
                   "status",
                   "sshd",
                   NULL);
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
        }

        fprintf(stderr, "Invalid set command\n");
        return 1;
    }

    // ===== всё остальное запрещено =====
    fprintf(stderr, "Command not allowed\n");
    return 1;
}
