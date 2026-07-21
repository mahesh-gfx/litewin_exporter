#ifndef LITEWIN_SVC_H
#define LITEWIN_SVC_H

#include "http.h" /* http_handler_fn */

/* Run under the Windows SCM if started as a service; otherwise (error 1063,
 * "no service controller") fall through to a plain console run. Blocks until
 * the server stops. Returns the http_serve() result. */
int svc_start(unsigned long bind_addr, int port, http_handler_fn handler);

#endif
