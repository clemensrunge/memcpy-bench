#ifndef CPUID_FUNCTIONS_H
#define CPUID_FUNCTIONS_H

#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CPU_MAX_CACHE_ENTRIES 8

typedef struct {
    unsigned int level;
    char type[16];
    unsigned int size_kb;
} CpuCacheEntry;

typedef struct {
    char vendor[13];
    char brand[49]; // empty string when the CPU doesn't report one
    bool deterministic_supported;
    bool legacy_amd;
    unsigned int cache_count;
    CpuCacheEntry caches[CPU_MAX_CACHE_ENTRIES];
} CpuInfo;

CpuInfo cpuid_get_info(void);
void cpuid_print_info(const CpuInfo* info);

// Largest cache size in KB detected at the given level (1-3), or 0 if the
// level wasn't detected.
unsigned int cpuid_cache_size_kb(const CpuInfo* info, unsigned int level);

#ifdef __cplusplus
}
#endif

#endif // CPUID_FUNCTIONS_H
