#ifndef LITEWIN_COLLECTOR_SYSTEM_H
#define LITEWIN_COLLECTOR_SYSTEM_H

#include "textfmt.h" /* buf_t */

int system_init(void);        /* add the System PDH counters */
int collect_system(buf_t *b); /* processes, threads, exception dispatches */

#endif
