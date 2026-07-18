#include <stdio.h>
#include <string.h>

#include "http.h"
#include "log.h"

#define LITEWIN_NAME    "litewin_exporter"
#define LITEWIN_VERSION "0.1.0-dev"
#define DEFAULT_PORT    9182

static char *landing(const char *path, const char **content_type)
{
    if (strcmp(path, "/") != 0)
        return NULL;
    *content_type = "text/html";
    return strdup("<html><body><h1>" LITEWIN_NAME " " LITEWIN_VERSION "</h1>"
                  "<p><a href=\"/metrics\">/metrics</a></p></body></html>");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    log_msg("%s %s", LITEWIN_NAME, LITEWIN_VERSION);
    return http_serve(0, DEFAULT_PORT, landing);
}
