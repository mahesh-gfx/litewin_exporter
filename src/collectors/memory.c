#include "collectors/memory.h"

/* Byte counts up to 2^53 (~9 PB) are exact in a double, so tf_sample's %.15g
 * renders real RAM/pagefile sizes losslessly. */
void memory_format(buf_t *b, unsigned long long total_phys,
                   unsigned long long avail_phys,
                   unsigned long long commit_limit)
{
    tf_family(b, "windows_memory_physical_total_bytes", "gauge",
              "The amount of actual physical memory in bytes.");
    tf_sample(b, "windows_memory_physical_total_bytes", NULL, (double)total_phys);

    tf_family(b, "windows_memory_physical_free_bytes", "gauge",
              "The amount of physical memory currently available in bytes.");
    tf_sample(b, "windows_memory_physical_free_bytes", NULL, (double)avail_phys);

    tf_family(b, "windows_memory_commit_limit", "gauge",
              "Amount of virtual memory in bytes that can be committed without "
              "extending the paging file(s).");
    tf_sample(b, "windows_memory_commit_limit", NULL, (double)commit_limit);
}

#ifdef _WIN32
#include <windows.h>

int collect_memory(buf_t *b)
{
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof ms;
    if (!GlobalMemoryStatusEx(&ms))
        return 0;
    memory_format(b, ms.ullTotalPhys, ms.ullAvailPhys, ms.ullTotalPageFile);
    return 1;
}
#endif
