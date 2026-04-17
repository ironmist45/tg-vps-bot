#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>

// ===== безопасная проверка IP =====

static int is_safe_ip(const char *ip) {
    if (!ip) return 0;

    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip, &(sa.sin_addr)) == 1;
}

// ===== main =====

int main(int argc, char *argv[]) {

    if (argc < 2) {
        fprintf(stderr, "Usage: f2b-wrapper <command>\n");
        return 1;
    }

    if (geteuid() != 0) {
        fprintf(stderr, "Must run as root\n");
        return 1;
    }

    // ===== status =====
    if (strcmp(argv[1], "status") == 0) {

        if (argc == 2) {
            execl("fail2ban-client",
                   "fail2ban-client",
                   "status",
                   NULL);
            perror("execlp: fail2ban-client exec failed");
            return 1;
        }

        if (argc == 3 && strcmp(argv[2], "sshd") == 0) {
            execl("fail2ban-client",
                   "fail2ban-client",
                   "status",
                   "sshd",
                   NULL);
            fprintf(stderr, "Execution failed\n");
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

            execl("fail2ban-client",
                   "fail2ban-client",
                   "set",
                   "sshd",
                   argv[3],
                   argv[4],
                   NULL);
            fprintf(stderr, "Execution failed\n");
            return 1;
        }

        fprintf(stderr, "Invalid set command\n");
        return 1;
    }

    // ===== всё остальное запрещено =====
    fprintf(stderr, "Command not allowed\n");
    return 1;
}
