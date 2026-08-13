#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

// Header-only helpers shared between the C modules and CpuIdFunctions.cpp.

// Trims leading/trailing spaces in place.
static inline void str_trim(char* s)
{
    char* start = s;
    while (*start == ' ')
    {
        ++start;
    }
    if (start != s)
    {
        memmove(s, start, strlen(start) + 1);
    }
    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == ' ')
    {
        s[--len] = '\0';
    }
}

// Single owner of the size-display convention used across the program:
// below 1 MB a size reads in KB, at or above in MB. Returns the scaled
// value and sets *unit_out to "KB" or "MB".
static inline unsigned int size_kb_for_display(unsigned int size_kb, const char** unit_out)
{
    if (size_kb < 1024)
    {
        *unit_out = "KB";
        return size_kb;
    }
    *unit_out = "MB";
    return size_kb / 1024;
}

#endif // UTIL_H
