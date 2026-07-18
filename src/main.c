#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "http.h"
#include "log.h"
#include "textfmt.h"
#include "version.h"

static config_t g_cfg;
static time_t g_start_time;

static char *metrics_body(void)
{
    buf_t b;
    buf_init(&b);
    tf_family(&b, "process_start_time_seconds", "gauge",
              "Start time of the process since unix epoch in seconds.");
    tf_sample(&b, "process_start_time_seconds", NULL, (double)g_start_time);
    return b.p; /* malloc'd; http.c frees it (NULL on alloc failure -> 404) */
}

static char *handler(const char *path, const char **content_type)
{
    if (strcmp(path, g_cfg.metrics_path) == 0) {
        *content_type = "text/plain; version=0.0.4; charset=utf-8";
        return metrics_body();
    }
    if (strcmp(path, "/") == 0) {
        char page[512];
        snprintf(page, sizeof page,
                 "<html><body><h1>" LITEWIN_NAME " " LITEWIN_VERSION "</h1>"
                 "<p><a href=\"%s\">%s</a></p></body></html>",
                 g_cfg.metrics_path, g_cfg.metrics_path);
        *content_type = "text/html";
        return strdup(page);
    }
    return NULL;
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

    g_start_time = time(NULL);
    memcpy(&bind_addr, g_cfg.bind_ip, 4); /* already network byte order */
    log_msg("%s %s", LITEWIN_NAME, LITEWIN_VERSION);
    return http_serve(bind_addr, g_cfg.port, handler);
}
