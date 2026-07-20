#ifndef LITEWIN_COLLECTOR_MEMORY_H
#define LITEWIN_COLLECTOR_MEMORY_H

#include "textfmt.h" /* buf_t */

/* Format the memory family from raw byte counts. Pure C (no windows.h) so it
 * unit-tests natively; the gather feeds it real values. */
void memory_format(buf_t *b, unsigned long long total_phys,
                   unsigned long long avail_phys,
                   unsigned long long commit_limit);

/* Gather (GlobalMemoryStatusEx) + format. 1 = ok, 0 = failed. Windows only. */
int collect_memory(buf_t *b);

#endif
