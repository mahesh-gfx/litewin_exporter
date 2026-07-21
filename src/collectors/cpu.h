#ifndef LITEWIN_COLLECTOR_CPU_H
#define LITEWIN_COLLECTOR_CPU_H

#include "textfmt.h" /* buf_t */

/* Pure: PDH "% * Time" raw counters are in 100 ns units -> seconds. */
double cpu_ticks_to_seconds(long long ticks_100ns);

/* Pure: emit one windows_cpu_time_total sample, windows_exporter's
 * core="socket,core" label format (socket hardcoded 0). */
void cpu_time_sample(buf_t *b, const char *core, const char *mode,
                     long long ticks_100ns);

/* Windows gather: add the Processor counters (init), then per-core times +
 * nominal frequency + logical processor count (collect). */
int cpu_init(void);
int collect_cpu(buf_t *b);

#endif
