#ifndef MEMORY_INFO_H
#define MEMORY_INFO_H

#include <stdbool.h>

#define MEMORY_MAX_MODULES 16

typedef struct {
    char manufacturer[32];
    char type[16]; // e.g. "DDR4"
    unsigned int speed_mts; // MT/s (mega-transfers per second)
    unsigned int size_mb;
} MemoryModule;

typedef struct {
    bool available;
    unsigned int module_count;
    MemoryModule modules[MEMORY_MAX_MODULES];
    // Number of distinct memory channels populated, inferred from dmidecode's
    // per-module "Bank Locator" grouping. 0 if it couldn't be determined.
    unsigned int channel_count;
} MemoryInfo;

// Reads installed RAM module info (manufacturer, type, speed) from the
// system's SMBIOS tables via `dmidecode`. This data is only readable as
// root, so on a non-root run `available` is false -- there is no portable,
// unprivileged way to get it.
MemoryInfo memory_get_info(void);
void memory_print_info(const MemoryInfo* info);

// If not running as root and stdin is an interactive terminal, explains that
// memory module info needs root and offers to re-exec the program under
// `sudo` (which shows sudo's normal password prompt). On "no", a non-tty
// stdin, or a failed re-exec, this just returns and the run continues
// without memory info. Never blocks a non-interactive run.
void memory_maybe_prompt_elevate(int argc, char** argv);

#endif // MEMORY_INFO_H
