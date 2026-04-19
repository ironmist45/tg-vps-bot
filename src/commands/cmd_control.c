#include "commands.h"
#include "utils.h"
#include "security.h"

#include <stdio.h>
#include <signal.h>

// extern globals
extern volatile sig_atomic_t g_shutdown_requested;
extern long g_reboot_requested_by;

