#include <stdio.h>
#include <string.h>

#include "config.h"
#include "http.h"
#include "log.h"
#include "version.h"

static config_t g_cfg;

static char *landing(const char *path, const char **content_type)
{
    char page[512];

    if (strcmp(path, "/") != 0)
        return NULL;
    snprintf(page, sizeof page,
             "<html><body><h1>" LITEWIN_NAME " " LITEWIN_VERSION "</h1>"
             "<p><a href=\"%s\">%s</a></p></body></html>",
             g_cfg.metrics_path, g_cfg.metrics_path);
    *content_type = "text/html";
    return strdup(page);
}

int main(int argc, char **argv)
{
    unsigned long bind_addr = 0;

    switch (config_parse(&g_cfg, argc, argv)) {
    case CONFIG_ERR:
        return 2;
    case CONFIG_EXIT0:
        return 0;
    case CONFIG_OK:
        break;
    }

    memcpy(&bind_addr, g_cfg.bind_ip, 4); /* already network byte order */
    log_msg("%s %s", LITEWIN_NAME, LITEWIN_VERSION);
    return http_serve(bind_addr, g_cfg.port, landing);
}
