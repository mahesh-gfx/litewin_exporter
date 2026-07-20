#include "config.h"
#include "version.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    printf("usage: %s [flags]\n"
           "\n"
           "  --web.listen-address ADDR   address for the metrics endpoint, e.g. \":%d\",\n"
           "                              \"%d\" or \"192.168.1.10:%d\" (default \":%d\")\n"
           "  --telemetry.addr ADDR       alias of --web.listen-address (windows_exporter compat)\n"
           "  --telemetry.path PATH       URL path for metrics (default \"/metrics\")\n"
           "  --collectors.enabled LIST   comma-separated collectors to enable; the token\n"
           "                              [defaults] expands to all of them (default: all)\n"
           "  --collectors.print          print available collectors and exit\n"
           "  --version                   print version and exit\n"
           "  --help                      this text\n"
           "\nAll flags also accept the --flag=value form.\n",
           LITEWIN_NAME, DEFAULT_PORT, DEFAULT_PORT, DEFAULT_PORT, DEFAULT_PORT);
}

/* matches "--name" or "--name=..."; sets *eq to the '=' if present */
static int flag_is(const char *arg, const char *name, const char **eq)
{
    size_t n = strlen(name);
    if (strncmp(arg, name, n) != 0)
        return 0;
    if (arg[n] == '\0') {
        *eq = NULL;
        return 1;
    }
    if (arg[n] == '=') {
        *eq = arg + n;
        return 1;
    }
    return 0;
}

/* accepts "--flag value" and "--flag=value"; NULL if the value is missing */
static const char *flag_value(int argc, char **argv, int *i, const char *eq)
{
    if (eq)
        return eq + 1;
    if (*i + 1 < argc)
        return argv[++*i];
    fprintf(stderr, "error: flag %s requires a value\n", argv[*i]);
    return NULL;
}

/* strict dotted-quad parse; out is in network byte order */
static int parse_ip4(const char *s, unsigned char out[4])
{
    int o[4], i;
    char extra;
    if (sscanf(s, "%d.%d.%d.%d%c", &o[0], &o[1], &o[2], &o[3], &extra) != 4)
        return 0;
    for (i = 0; i < 4; i++) {
        if (o[i] < 0 || o[i] > 255)
            return 0;
        out[i] = (unsigned char)o[i];
    }
    return 1;
}

/* ":9182", "9182" or "ip:9182" */
static int set_listen_address(config_t *cfg, const char *v)
{
    const char *colon = strrchr(v, ':');
    const char *portstr = colon ? colon + 1 : v;
    char host[64];
    char extra;
    int port;

    host[0] = '\0';
    if (colon) {
        size_t hl = (size_t)(colon - v);
        if (hl >= sizeof host) {
            fprintf(stderr, "error: bind host too long in \"%s\"\n", v);
            return 0;
        }
        memcpy(host, v, hl);
        host[hl] = '\0';
    }

    if (sscanf(portstr, "%d%c", &port, &extra) != 1 || port <= 0 || port > 65535) {
        fprintf(stderr, "error: invalid port in listen address \"%s\"\n", v);
        return 0;
    }
    cfg->port = port;

    if (host[0] && !parse_ip4(host, cfg->bind_ip)) {
        fprintf(stderr,
                "error: invalid bind IP \"%s\" (use a numeric IPv4 address or omit the host)\n",
                host);
        return 0;
    }
    return 1;
}

config_result_t config_parse(config_t *cfg, int argc, char **argv)
{
    int i;
    const char *eq;

    memset(cfg, 0, sizeof *cfg);
    cfg->port = DEFAULT_PORT;
    strcpy(cfg->metrics_path, "/metrics");

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (flag_is(a, "--web.listen-address", &eq) ||
            flag_is(a, "--telemetry.addr", &eq)) {
            const char *v = flag_value(argc, argv, &i, eq);
            if (!v || !set_listen_address(cfg, v))
                return CONFIG_ERR;
        } else if (flag_is(a, "--telemetry.path", &eq)) {
            const char *v = flag_value(argc, argv, &i, eq);
            if (!v)
                return CONFIG_ERR;
            if (v[0] != '/') {
                fprintf(stderr, "error: --telemetry.path must start with '/'\n");
                return CONFIG_ERR;
            }
            if (strlen(v) >= sizeof cfg->metrics_path) {
                fprintf(stderr, "error: --telemetry.path too long\n");
                return CONFIG_ERR;
            }
            strcpy(cfg->metrics_path, v);
        } else if (flag_is(a, "--collectors.enabled", &eq)) {
            const char *v = flag_value(argc, argv, &i, eq);
            if (!v)
                return CONFIG_ERR;
            if (strlen(v) >= sizeof cfg->collectors_enabled) {
                fprintf(stderr, "error: --collectors.enabled list too long\n");
                return CONFIG_ERR;
            }
            strcpy(cfg->collectors_enabled, v);
        } else if (strcmp(a, "--collectors.print") == 0) {
            cfg->print_collectors = 1;
            return CONFIG_OK; /* main lists the registry and exits 0 */
        } else if (strcmp(a, "--version") == 0) {
            printf("%s %s\n", LITEWIN_NAME, LITEWIN_VERSION);
            return CONFIG_EXIT0;
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage();
            return CONFIG_EXIT0;
        } else {
            fprintf(stderr, "error: unknown flag \"%s\" (see --help)\n", a);
            return CONFIG_ERR;
        }
    }
    return CONFIG_OK;
}
