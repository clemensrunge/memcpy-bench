#include "MemoryInfo.h"
#include "Util.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

void memory_maybe_prompt_elevate(int argc, char** argv)
{
    if (geteuid() == 0)
    {
        return;
    }

    if (!isatty(fileno(stdin)))
    {
        printf("Note: run as root to also show memory module info (manufacturer/type/speed).\n");
        return;
    }

    printf("Memory module info (manufacturer/type/speed) needs root to read.\n");
    printf("Re-run this program with sudo now? [y/N] ");
    fflush(stdout);

    int response = getchar();
    int c = response;
    while (c != '\n' && c != EOF)
    {
        c = getchar();
    }

    if (response != 'y' && response != 'Y')
    {
        return;
    }

    // argv has argc entries plus the NULL terminator already guaranteed by
    // the C standard; build "sudo <original argv>" with its own NULL end.
    char* sudo_argv[argc + 2];
    sudo_argv[0] = "sudo";
    for (int i = 0; i < argc; ++i)
    {
        sudo_argv[i + 1] = argv[i];
    }
    sudo_argv[argc + 1] = NULL;

    execvp("sudo", sudo_argv);

    // Only reached if execvp failed to even start sudo.
    printf("Could not run sudo -- continuing without root.\n");
}

// dmidecode fills these in for SMBIOS fields the BIOS never populated --
// treat them the same as "no data" rather than printing them as if real.
static bool is_placeholder_value(const char* s)
{
    return strcasecmp(s, "Unknown") == 0 ||
           strcasecmp(s, "Not Specified") == 0 ||
           strcasecmp(s, "To Be Filled By O.E.M.") == 0 ||
           strcasecmp(s, "N/A") == 0;
}

// Returns the value part of a "Prefix: value" line (leading spaces skipped)
// if the line starts with prefix, or NULL if it doesn't.
static const char* field_value(const char* line, const char* prefix)
{
    size_t prefix_len = strlen(prefix);
    if (strncmp(line, prefix, prefix_len) != 0)
    {
        return NULL;
    }
    const char* val = line + prefix_len;
    while (*val == ' ')
    {
        ++val;
    }
    return val;
}

// Copies a string field, trims it, and blanks it if it is a BIOS placeholder.
static void copy_clean_field(char* dest, size_t dest_size, const char* val)
{
    snprintf(dest, dest_size, "%s", val);
    str_trim(dest);
    if (is_placeholder_value(dest))
    {
        dest[0] = '\0';
    }
}

// Tracks distinct "Bank Locator" strings (dmidecode's channel grouping,
// e.g. "P0 CHANNEL A") seen across populated modules, so we can count how
// many memory channels are actually populated.
static void record_channel_key(char keys[][32], unsigned int* count, const char* key)
{
    if (key[0] == '\0')
    {
        return;
    }
    for (unsigned int i = 0; i < *count; ++i)
    {
        if (strcmp(keys[i], key) == 0)
        {
            return;
        }
    }
    if (*count < MEMORY_MAX_MODULES)
    {
        snprintf(keys[*count], 32, "%s", key);
        (*count)++;
    }
}

// Commits a fully parsed module; the single place the accept conditions live.
static void flush_module(MemoryInfo* info, const MemoryModule* module, bool have_size,
                          char channel_keys[][32], unsigned int* channel_key_count,
                          const char* bank_locator)
{
    if (!have_size || info->module_count >= MEMORY_MAX_MODULES)
    {
        return;
    }
    info->modules[info->module_count++] = *module;
    record_channel_key(channel_keys, channel_key_count, bank_locator);
}

MemoryInfo memory_get_info(void)
{
    MemoryInfo info;
    memset(&info, 0, sizeof(info));

    // Per-DIMM manufacturer/type/speed lives in SMBIOS SPD data, which is
    // only readable as root -- there's no portable unprivileged path to it.
    if (geteuid() != 0)
    {
        return info;
    }

    FILE* pipe = popen("dmidecode -t 17 2>/dev/null", "r");
    if (!pipe)
    {
        return info;
    }

    char line[256];
    bool in_module = false;
    bool have_size = false;
    MemoryModule current;
    memset(&current, 0, sizeof(current));
    char current_bank_locator[32] = { 0 };

    char channel_keys[MEMORY_MAX_MODULES][32];
    unsigned int channel_key_count = 0;

    while (fgets(line, sizeof(line), pipe))
    {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        {
            line[--len] = '\0';
        }

        char* trimmed = line;
        while (*trimmed == '\t' || *trimmed == ' ')
        {
            ++trimmed;
        }

        if (strcmp(trimmed, "Memory Device") == 0)
        {
            if (in_module)
            {
                flush_module(&info, &current, have_size, channel_keys, &channel_key_count,
                             current_bank_locator);
            }
            memset(&current, 0, sizeof(current));
            current_bank_locator[0] = '\0';
            have_size = false;
            in_module = true;
            continue;
        }

        if (!in_module)
        {
            continue;
        }

        const char* val;
        if ((val = field_value(trimmed, "Size:")) != NULL)
        {
            unsigned int size_val = 0;
            char unit[8] = { 0 };
            if (sscanf(val, "%u %7s", &size_val, unit) == 2)
            {
                if (strcmp(unit, "MB") == 0)
                {
                    current.size_mb = size_val;
                }
                else if (strcmp(unit, "GB") == 0)
                {
                    current.size_mb = size_val * 1024;
                }
                have_size = (current.size_mb > 0);
            }
        }
        else if ((val = field_value(trimmed, "Type:")) != NULL)
        {
            copy_clean_field(current.type, sizeof(current.type), val);
        }
        else if ((val = field_value(trimmed, "Bank Locator:")) != NULL)
        {
            copy_clean_field(current_bank_locator, sizeof(current_bank_locator), val);
        }
        else if ((val = field_value(trimmed, "Manufacturer:")) != NULL)
        {
            copy_clean_field(current.manufacturer, sizeof(current.manufacturer), val);
        }
        else if ((val = field_value(trimmed, "Speed:")) != NULL ||
                 (val = field_value(trimmed, "Configured Memory Speed:")) != NULL)
        {
            unsigned int speed_val = 0;
            // "Configured Memory Speed:" is reported after "Speed:" for the
            // same module, so letting it overwrite is intentional -- it's
            // the more accurate figure (accounts for XMP/EXPO profiles).
            if (sscanf(val, "%u", &speed_val) == 1)
            {
                current.speed_mts = speed_val;
            }
        }
    }

    if (in_module)
    {
        flush_module(&info, &current, have_size, channel_keys, &channel_key_count,
                     current_bank_locator);
    }

    pclose(pipe);

    if (info.module_count == 0)
    {
        return info;
    }

    // Only trust the channel count if it evenly divides the populated
    // modules (e.g. 4 modules / 2 channels) -- otherwise the Bank Locator
    // grouping didn't cleanly map to channels and we'd rather say "unknown"
    // than print a wrong number.
    if (channel_key_count > 0 && info.module_count % channel_key_count == 0)
    {
        info.channel_count = channel_key_count;
    }

    info.available = true;
    return info;
}

void memory_print_info(const MemoryInfo* info)
{
    // Silently skip the whole section when we couldn't get real data (no
    // root, dmidecode missing, etc.) rather than printing an error.
    if (!info->available)
    {
        return;
    }

    bool uniform = true;
    for (unsigned int i = 1; i < info->module_count; ++i)
    {
        if (info->modules[i].size_mb != info->modules[0].size_mb ||
            strcmp(info->modules[i].type, info->modules[0].type) != 0 ||
            info->modules[i].speed_mts != info->modules[0].speed_mts)
        {
            uniform = false;
            break;
        }
    }

    if (uniform)
    {
        const MemoryModule* m = &info->modules[0];
        printf("Memory: %ux %.0f GB %s @ %u MT/s", info->module_count, m->size_mb / 1024.0, m->type,
               m->speed_mts);
        if (m->manufacturer[0] != '\0')
        {
            printf(" (%s)", m->manufacturer);
        }

        // Theoretical peak: DDR uses a 64-bit (8-byte) wide bus per channel,
        // so bytes/s = transfers/s * 8. Reported in decimal GB/s to match
        // how RAM speed is conventionally marketed (e.g. "DDR4-3200 = 25.6
        // GB/s per channel"), unlike this program's binary-based GB/s
        // elsewhere for measured memcpy throughput.
        double per_channel_gbps = m->speed_mts * 8.0 / 1000.0;
        if (info->channel_count > 0)
        {
            printf(" -- theoretical peak: %.1f GB/s (%u channels x %.1f GB/s)", per_channel_gbps * info->channel_count,
                   info->channel_count, per_channel_gbps);
        }
        else
        {
            printf(" -- theoretical peak: %.1f GB/s per channel (channel count unknown)", per_channel_gbps);
        }
        putchar('\n');
    }
    else
    {
        for (unsigned int i = 0; i < info->module_count; ++i)
        {
            const MemoryModule* m = &info->modules[i];
            printf("Memory[%u]: %u MB %s @ %u MT/s", i, m->size_mb, m->type, m->speed_mts);
            if (m->manufacturer[0] != '\0')
            {
                printf(" (%s)", m->manufacturer);
            }
            putchar('\n');
        }
    }
}
