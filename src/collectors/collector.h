#ifndef LITEWIN_COLLECTOR_H
#define LITEWIN_COLLECTOR_H

#include "textfmt.h" /* buf_t */

/* A collector: name doubles as the enable flag and the collector_success label.
 * A collector's .c includes only this header, textfmt.h, and what it measures —
 * never http/config or another collector. */
typedef struct {
    const char *name;
    int (*init)(void);       /* optional (NULL if none); once at startup if enabled */
    int (*collect)(buf_t *); /* append metric lines; 1 = ok, 0 = failed */
    int enabled;             /* set by collectors_resolve() */
} collector_t;

#endif
