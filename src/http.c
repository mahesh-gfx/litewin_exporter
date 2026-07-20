#include "http.h"
#include "log.h"

#include <winsock2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    SOCKET ls;
    struct sockaddr_in addr;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        log_msg("WSAStartup failed");
        return 1;
    }

    ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) {
        log_msg("socket() failed");
        return 1;
    }

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = bind_addr;
    addr.sin_port = htons((u_short)port);
    if (bind(ls, (struct sockaddr *)&addr, sizeof addr) == SOCKET_ERROR) {
        log_msg("bind(:%d) failed: %d", port, WSAGetLastError());
        return 1;
    }
    if (listen(ls, 8) == SOCKET_ERROR) {
        log_msg("listen failed");
        return 1;
    }

    log_msg("listening on :%d", port);
    for (;;) {
        SOCKET c = accept(ls, NULL, NULL);
        if (c == INVALID_SOCKET)
            continue;
        handle_client(c, handler);
    }
}
