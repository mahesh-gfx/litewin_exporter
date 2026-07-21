#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "http.h"
#include "log.h"
#include "pdh_util.h"
#include "registry.h"
#include "svc.h"
#include "textfmt.h"
#include "version.h"

static config_t g_cfg;
static time_t g_start_time;

static char *metrics_body(void)
{
    collector_t *cols;
    size_t n, i;
    int ok[64]; /* fixed cap; collectors number ~10, bump if it ever nears 64 */
    int any = 0;
    buf_t b;

    buf_init(&b);

    /* One PDH sample per scrape feeds all PDH-backed collectors (cpu, ...). */
    pdh_collect();

    /* Run enabled collectors first (they append their own families), then the
     * collector_success meta family so it groups at the bottom. */
    cols = registry_collectors(&n);
    if (n > sizeof ok / sizeof ok[0])
        n = sizeof ok / sizeof ok[0];
    for (i = 0; i < n; i++)
        if (cols[i].enabled) {
            ok[i] = cols[i].collect ? cols[i].collect(&b) : 1;
            any = 1;
        }

    tf_family(&b, "process_start_time_seconds", "gauge",
              "Start time of the process since unix epoch in seconds.");
    tf_sample(&b, "process_start_time_seconds", NULL, (double)g_start_time);

    if (any) {
        tf_family(&b, "windows_exporter_collector_success", "gauge",
                  "1 if the collector succeeded, 0 otherwise.");
        for (i = 0; i < n; i++)
            if (cols[i].enabled) {
                char labels[128];
                snprintf(labels, sizeof labels, "collector=\"%s\"", cols[i].name);
                tf_sample(&b, "windows_exporter_collector_success", labels, ok[i]);
            }
    }
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
    collector_t *cols;
    size_t n, n_i;
    char unknown[64];

    switch (config_parse(&g_cfg, argc, argv)) {
    case CONFIG_ERR:
        return 2;
    case CONFIG_EXIT0:
        return 0;
    case CONFIG_OK:
        break;
    }

    cols = registry_collectors(&n);
    if (g_cfg.print_collectors) {
        collectors_print(cols, n);
        return 0;
    }
    if (!collectors_resolve(cols, n, g_cfg.collectors_enabled, unknown, sizeof unknown)) {
        fprintf(stderr, "error: unknown collector \"%s\" in --collectors.enabled\n", unknown);
        fprintf(stderr, "run with --collectors.print to list the available collectors\n");
        return 2;
    }

    /* One-time per-collector startup (e.g. cpu opens the PDH query + counters);
     * then prime PDH so the first scrape already has data. */
    for (n_i = 0; n_i < n; n_i++)
        if (cols[n_i].enabled && cols[n_i].init && !cols[n_i].init())
            log_msg("warning: collector %s init failed", cols[n_i].name);
    pdh_collect();

    g_start_time = time(NULL);
    memcpy(&bind_addr, g_cfg.bind_ip, 4); /* already network byte order */
    log_msg("%s %s", LITEWIN_NAME, LITEWIN_VERSION);
    /* Runs under the SCM if launched as a service, else console mode. */
    return svc_start(bind_addr, g_cfg.port, handler);
}
