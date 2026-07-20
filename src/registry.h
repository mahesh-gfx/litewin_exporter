#ifndef LITEWIN_REGISTRY_H
#define LITEWIN_REGISTRY_H

#include <stddef.h>

#include "collectors/collector.h"

/* The built-in collector set. First collector us yet to be added */
collector_t *registry_collectors(size_t *n);

/* Resolve a --collectors.enabled list against cols[0..n), marking .enabled.
 *   NULL or ""    -> all enabled (the default)
 *   "[defaults]"  -> all enabled
 *   "a,b,c"       -> only those; leading spaces per token tolerated
 * On an unknown name: returns 0 and copies it into `unknown`. Otherwise 1.
 * Pure: no windows.h, no exit(), no globals. */
int collectors_resolve(collector_t *cols, size_t n, const char *list,
                       char *unknown, size_t unknown_sz);

/* Print each collector name, one per line (for --collectors.print). */
void collectors_print(collector_t *cols, size_t n);

#endif
