#include "http.h"
#include "log.h"

#include <winsock2.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shared with http_stop() so the service control handler can end the loop. */
static volatile LONG g_stop;
static SOCKET g_listen_sock = INVALID_SOCKET;

static void send_all(SOCKET s, const char *p, size_t n)
{
    while (n > 0) {
        int w = send(s, p, (int)n, 0);
        if (w <= 0)
            return;
        p += w;
        n -= (size_t)w;
    }
}

static void handle_client(SOCKET c, http_handler_fn handler)
{
    char req[2048]; /* cap request read; anything longer is truncated */
    int n = recv(c, req, sizeof req - 1, 0);
    if (n <= 0) {
        closesocket(c);
        return;
    }
    req[n] = '\0';

    if (strncmp(req, "GET ", 4) == 0) {
        char *path = req + 4;
        char *end = strpbrk(path, " ?\r\n");
        if (end)
            *end = '\0';

        const char *ctype = "text/plain";
        char *body = handler(path, &ctype);
        if (body) {
            char hdr[256];
            snprintf(hdr, sizeof hdr,
                     "HTTP/1.0 200 OK\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %lu\r\n"
                     "Connection: close\r\n\r\n",
                     ctype, (unsigned long)strlen(body));
            send_all(c, hdr, strlen(hdr));
            send_all(c, body, strlen(body));
            free(body);
            closesocket(c);
            return;
        }
    }

    {
        static const char nf[] =
            "HTTP/1.0 404 Not Found\r\nConnection: close\r\n\r\nnot found\n";
        send_all(c, nf, sizeof nf - 1);
    }
    closesocket(c);
}

int http_serve(unsigned long bind_addr, int port, http_handler_fn handler)
{
    WSADATA wsa;
    struct sockaddr_in addr;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        log_msg("WSAStartup failed");
        return 1;
    }

    g_listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen_sock == INVALID_SOCKET) {
        log_msg("socket() failed");
        return 1;
    }

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = bind_addr;
    addr.sin_port = htons((u_short)port);
    if (bind(g_listen_sock, (struct sockaddr *)&addr, sizeof addr) == SOCKET_ERROR) {
        log_msg("bind(:%d) failed: %d", port, WSAGetLastError());
        return 1;
    }
    /* SOMAXCONN: measured with backlog 8, >=20 tight-loop scrapers overflowed
     * the queue and got connection-refused; the stack-sized queue absorbs
     * bursts while the single service thread drains them. */
    if (listen(g_listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        log_msg("listen failed");
        return 1;
    }

    log_msg("listening on :%d", port);
    while (!g_stop) {
        SOCKET c = accept(g_listen_sock, NULL, NULL);
        if (c == INVALID_SOCKET) /* accept fails when http_stop() closes the socket */
            continue;
        /* Single-threaded: an idle/half-open client must not wedge the loop.
           Winsock takes the timeout as a DWORD of milliseconds. */
        {
            DWORD tmo = 5000;
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof tmo);
            setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tmo, sizeof tmo);
        }
        handle_client(c, handler);
    }
    closesocket(g_listen_sock);
    g_listen_sock = INVALID_SOCKET;
    WSACleanup();
    return 0;
}

void http_stop(void)
{
    InterlockedExchange(&g_stop, 1);
    if (g_listen_sock != INVALID_SOCKET)
        closesocket(g_listen_sock); /* unblocks the accept() above */
}
