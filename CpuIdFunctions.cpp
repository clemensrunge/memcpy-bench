#include "CpuIdFunctions.h"
#include "Util.h"

#include <cstdio>
#include <cstring>

#ifdef _MSC_VER
    #include <intrin.h>
#else
    #include <cpuid.h>
#endif

namespace {

// Cross-platform CPUID wrapper
void cpuid(unsigned int leaf, unsigned int subleaf,
           unsigned int& eax, unsigned int& ebx, unsigned int& ecx, unsigned int& edx)
{
#ifdef _MSC_VER
    int regs[4];
    __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(subleaf));
    eax = static_cast<unsigned int>(regs[0]);
    ebx = static_cast<unsigned int>(regs[1]);
    ecx = static_cast<unsigned int>(regs[2]);
    edx = static_cast<unsigned int>(regs[3]);
#else
    __cpuid_count(leaf, subleaf, eax, ebx, ecx, edx);
#endif
}

const char* cache_type_name(unsigned int cache_type)
{
    switch (cache_type)
    {
        case 1: return "Data";
        case 2: return "Instruction";
        case 3: return "Unified";
        default: return "Unknown";
    }
}

void add_cache_entry(CpuInfo& info, unsigned int level, const char* type, unsigned int size_kb)
{
    if (info.cache_count >= CPU_MAX_CACHE_ENTRIES)
    {
        return;
    }
    CpuCacheEntry& entry = info.caches[info.cache_count++];
    entry.level = level;
    std::snprintf(entry.type, sizeof(entry.type), "%s", type);
    entry.size_kb = size_kb;
}

} // namespace

CpuInfo cpuid_get_info(void)
{
    CpuInfo info{};
    info.deterministic_supported = true;
    info.legacy_amd = false;
    info.cache_count = 0;

    unsigned int eax, ebx, ecx, edx;

    // Vendor string
    cpuid(0, 0, eax, ebx, ecx, edx);
    std::memcpy(info.vendor + 0, &ebx, 4);
    std::memcpy(info.vendor + 4, &edx, 4);
    std::memcpy(info.vendor + 8, &ecx, 4);
    info.vendor[12] = '\0';

    unsigned int max_basic_leaf = eax;

    cpuid(0x80000000, 0, eax, ebx, ecx, edx);
    unsigned int max_extended_leaf = eax;

    // Processor brand string (e.g. "AMD Ryzen 9 5900X 12-Core Processor");
    // padded with spaces by the CPU, hence the trim. Left empty when the
    // extended leaves aren't supported.
    info.brand[0] = '\0';
    if (max_extended_leaf >= 0x80000004)
    {
        for (unsigned int leaf = 0x80000002; leaf <= 0x80000004; ++leaf)
        {
            unsigned int beax, bebx, becx, bedx;
            cpuid(leaf, 0, beax, bebx, becx, bedx);
            unsigned int offset = (leaf - 0x80000002) * 16;
            std::memcpy(info.brand + offset + 0, &beax, 4);
            std::memcpy(info.brand + offset + 4, &bebx, 4);
            std::memcpy(info.brand + offset + 8, &becx, 4);
            std::memcpy(info.brand + offset + 12, &bedx, 4);
        }
        info.brand[48] = '\0';
        str_trim(info.brand);
    }

    // Prefer the standard deterministic leaf if available
    unsigned int cache_leaf;
    if (max_basic_leaf >= 4)
    {
        cache_leaf = 4;
    }
    else if (max_extended_leaf >= 0x8000001D)
    {
        cache_leaf = 0x8000001D;
    }
    else
    {
        info.deterministic_supported = false;
        return info;
    }

    // Enumerate caches
    for (unsigned int i = 0;; ++i)
    {
        cpuid(cache_leaf, i, eax, ebx, ecx, edx);

        unsigned int cache_type = eax & 0x1F;
        if (cache_type == 0)
        {
            if (i == 0)
            {
                info.legacy_amd = true;
            }
            break;
        }

        unsigned int cache_level = (eax >> 5) & 0x7;
        unsigned int line_size   = (ebx & 0xFFF) + 1;
        unsigned int partitions  = ((ebx >> 12) & 0x3FF) + 1;
        unsigned int ways        = ((ebx >> 22) & 0x3FF) + 1;
        unsigned int sets        = ecx + 1;
        unsigned int cache_size  = ways * partitions * line_size * sets;

        add_cache_entry(info, cache_level, cache_type_name(cache_type), cache_size / 1024);
    }

    if (info.legacy_amd)
    {
        cpuid(0x80000005, 0, eax, ebx, ecx, edx);
        unsigned int l1d = (ecx >> 24) & 0xFF;
        unsigned int l1i = (edx >> 24) & 0xFF;
        add_cache_entry(info, 1, "Data", l1d);
        add_cache_entry(info, 1, "Instruction", l1i);

        cpuid(0x80000006, 0, eax, ebx, ecx, edx);
        unsigned int l2 = (ecx >> 16) & 0xFFFF;
        unsigned int l3 = ((edx >> 18) & 0x3FFF) * 512;

        if (l2)
        {
            add_cache_entry(info, 2, "Unified", l2);
        }
        if (l3)
        {
            add_cache_entry(info, 3, "Unified", l3);
        }
    }

    return info;
}

unsigned int cpuid_cache_size_kb(const CpuInfo* info, unsigned int level)
{
    unsigned int best = 0;
    for (unsigned int i = 0; i < info->cache_count; ++i)
    {
        if (info->caches[i].level == level && info->caches[i].size_kb > best)
        {
            best = info->caches[i].size_kb;
        }
    }
    return best;
}

void cpuid_print_info(const CpuInfo* info)
{
    if (info->brand[0] != '\0')
    {
        std::printf("CPU Model: %s\n", info->brand);
    }
    std::printf("CPU Vendor: %s\n", info->vendor);

    if (!info->deterministic_supported)
    {
        std::printf("Deterministic cache leaf not supported.\n");
        return;
    }

    if (info->legacy_amd)
    {
        std::printf("Using Legacy AMD method\n");
    }

    for (unsigned int i = 0; i < info->cache_count; ++i)
    {
        const CpuCacheEntry& c = info->caches[i];
        char label[32];
        std::snprintf(label, sizeof(label), "L%u %s Cache:", c.level, c.type);

        const char* unit;
        unsigned int value = size_kb_for_display(c.size_kb, &unit);
        std::printf("%-22s %6u %s\n", label, value, unit);
    }
}
