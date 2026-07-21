#ifndef LITEWIN_HTTP_H
#define LITEWIN_HTTP_H

/* Handler for a GET: return a malloc'd body for `path` (http.c frees it)
 * and set *content_type, or return NULL for a 404. */
typedef char *(*http_handler_fn)(const char *path, const char **content_type);

/* Serve HTTP/1.0, single-threaded, one connection per request, until stopped.
 * bind_addr is an IPv4 address in network byte order (0 = all interfaces).
 * Returns non-zero on setup failure, 0 after a clean http_stop(). */
int http_serve(unsigned long bind_addr, int port, http_handler_fn handler);

/* Stop the running http_serve() loop (called from the service control handler,
 * which runs on another thread). */
void http_stop(void);

#endif
