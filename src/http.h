#ifndef LITEWIN_HTTP_H
#define LITEWIN_HTTP_H

/* Handler for a GET: return a malloc'd body for `path` (http.c frees it)
 * and set *content_type, or return NULL for a 404. */
typedef char *(*http_handler_fn)(const char *path, const char **content_type);

/* Serve HTTP/1.0, single-threaded, one connection per request, forever.
 * bind_addr is an IPv4 address in network byte order (0 = all interfaces).
 * Returns non-zero on setup failure. */
int http_serve(unsigned long bind_addr, int port, http_handler_fn handler);

#endif
