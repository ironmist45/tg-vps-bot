#include "cmd_security.h"

#include "logger.h"
#include "exec.h"
#include "utils.h"
#include "security.h"

#include <stdio.h>
#include <string.h>

// ==== /fail2ban command ====

int cmd_fail2ban(int argc, char *argv[],
                 long chat_id,
                 char *resp, size_t size,
                 response_type_t *resp_type) {

    if (resp_type) *resp_type = RESP_PLAIN;

    if (!argv || !argv[0]) {
        snprintf(resp, size, "Invalid command");
        return -1;
    }

    REQUIRE_ACCESS(chat_id, argv[0], resp, size);

    if (argc < 2) {
        snprintf(resp, size,
            "🛡 Fail2Ban\n\n"
            "/fail2ban status\n"
            "/fail2ban status sshd\n"
            "/fail2ban ban <ip>\n"
            "/fail2ban unban <ip>");
        return 0;
    }

    if (strcmp(argv[1], "status") == 0) {

        if (argc == 2) {
            char *const args[] = {
                "sudo",
                "-n",
                "/usr/local/bin/f2b-wrapper",
                "status",
                NULL
        };

        char tmp[RESP_MAX];
        exec_result_t res;

        int rc = exec_command(args, tmp, sizeof(tmp), NULL, &res);

        // 🔴 1. Ошибка выполнения (fork/pipe/timeout и т.д.)
        if (rc != 0) {

            if (res.status == EXEC_TIMEOUT) {
                snprintf(resp, size, "❌ Fail2Ban timeout");
            }
            else if (res.status == EXEC_EXEC_FAILED) {
                    snprintf(resp, size,
                    "❌ Fail2Ban binary error\n"
                    "Possible GLIBC mismatch");
            }
            else {
                snprintf(resp, size,
                    "❌ Fail2Ban failed (%s)",
                    exec_status_str(res.status));
            }

            return 0;
        }

        // 🟡 2. Команда выполнилась, но exit != 0
        if (res.status == EXEC_EXIT_NONZERO) {
            safe_code_block(tmp, resp, size);
            return 0;
        }

        // 🟢 3. Успех
        safe_code_block(tmp, resp, size);
        return 0;
                  
    }
          
    else if (argc == 3 && strcmp(argv[2], "sshd") == 0) {
        char *const args[] = {
            "sudo",
            "-n",
            "/usr/local/bin/f2b-wrapper",
            "status",
            argv[2],
            NULL
        };

        char tmp[RESP_MAX];
        exec_result_t res;

        int rc = exec_command(args, tmp, sizeof(tmp), NULL, &res);

        // 🔴 1. Ошибка выполнения (fork/pipe/timeout и т.д.)
        if (rc != 0) {

            if (res.status == EXEC_TIMEOUT) {
                snprintf(resp, size, "❌ Fail2Ban timeout");
            }
            else if (res.status == EXEC_EXEC_FAILED) {
                    snprintf(resp, size,
                    "❌ Fail2Ban binary error\n"
                    "Possible GLIBC mismatch");
            }
            else {
                snprintf(resp, size,
                    "❌ Fail2Ban failed (%s)",
                    exec_status_str(res.status));
            }

            return 0;
        }

        // 🟡 2. Команда выполнилась, но exit != 0
        if (res.status == EXEC_EXIT_NONZERO) {
            safe_code_block(tmp, resp, size);
            return 0;
        }

        // 🟢 3. Успех
        safe_code_block(tmp, resp, size);
        return 0;
      
    }
      
    else {
        snprintf(resp, size, "Invalid status usage");
        return -1;
    }
}

    else if (strcmp(argv[1], "ban") == 0 && argc >= 3) {

        if (!is_safe_ip(argv[2])) {
            snprintf(resp, size, "Invalid IP");
            return -1;
        }

        LOG_STATE(LOG_WARN,
            "FAIL2BAN BAN: ip=%s (chat_id=%ld)",
            argv[2], chat_id);

        char *const args[] = {
            "sudo",
            "-n",
            "/usr/local/bin/f2b-wrapper",
            "set",
            "sshd",
            "banip",
            argv[2],
            NULL
        };

        char tmp[RESP_MAX];
        exec_result_t res;

        if (exec_command(args, tmp, sizeof(tmp), NULL, &res) != 0) {

            if (res.status == EXEC_TIMEOUT) {
              snprintf(resp, size, "❌ Ban timeout");
            }
            else if (res.status == EXEC_EXEC_FAILED) {
                snprintf(resp, size, "❌ Fail2Ban wrapper broken");
            }
            else {
                snprintf(resp, size,
                    "❌ Ban failed (%s)",
                    exec_status_str(res.status));
            }

            return -1;
      }

      if (res.status == EXEC_EXIT_NONZERO) {
          safe_code_block(tmp, resp, size);
          return -1;
      }

      snprintf(resp, size, "✅ IP banned");
      return 0;
      
}

    else if (strcmp(argv[1], "unban") == 0 && argc >= 3) {

        if (!is_safe_ip(argv[2])) {
            snprintf(resp, size, "Invalid IP");
            return -1;
        }

        LOG_STATE(LOG_WARN,
            "FAIL2BAN UNBAN: ip=%s (chat_id=%ld)",
            argv[2], chat_id);

        char *const args[] = {
            "sudo",
            "-n",
            "/usr/local/bin/f2b-wrapper",
            "set",
            "sshd",
            "unbanip",
            argv[2],
            NULL
        };

        char tmp[RESP_MAX];
        exec_result_t res;

        if (exec_command(args, tmp, sizeof(tmp), NULL, &res) != 0) {

            if (res.status == EXEC_TIMEOUT) {
                snprintf(resp, size, "❌ Unban timeout");
            }
            else if (res.status == EXEC_EXEC_FAILED) {
                snprintf(resp, size, "❌ Fail2Ban wrapper broken");
            }
            else {
                snprintf(resp, size,
                    "❌ Unban failed (%s)",
                    exec_status_str(res.status));
            }

            return -1;
        }

        if (res.status == EXEC_EXIT_NONZERO) {
            safe_code_block(tmp, resp, size);
            return -1;
        }

        snprintf(resp, size, "✅ IP unbanned");
        return 0;
      
}

    else {
        snprintf(resp, size, "Invalid fail2ban command");
        return -1;
    }
  
}
