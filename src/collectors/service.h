#ifndef LITEWIN_COLLECTOR_SERVICE_H
#define LITEWIN_COLLECTOR_SERVICE_H

#include "textfmt.h" /* buf_t */

/* Pure: Win32 SERVICE_* state code (1..7) -> lowercase state name. */
const char *service_state_name(unsigned state);

/* Pure: emit the 7-state one-hot windows_service_state{name,state} lines for
 * one service (name is lowercased + label-escaped here). Exactly one line is 1. */
void service_emit_states(buf_t *b, const char *name, const char *current);

int collect_service(buf_t *b); /* Windows gather via the SCM */

#endif
