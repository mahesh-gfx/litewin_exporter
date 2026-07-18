#ifndef LITEWIN_CONFIG_H
#define LITEWIN_CONFIG_H

/* Command-line configuration. Pure C, no windows.h — unit-tested natively. */

#define DEFAULT_PORT 9182

typedef struct {
    int port;                 /* listen port (default 9182) */
    unsigned char bind_ip[4]; /* IPv4, network byte order; all-zero = all interfaces */
    char metrics_path[128];   /* URL path for metrics (default "/metrics") */
} config_t;

typedef enum {
    CONFIG_OK = 0, /* cfg populated; run the server            */
    CONFIG_EXIT0,  /* --help/--version handled; caller exits 0 */
    CONFIG_ERR     /* error printed to stderr; caller exits 2  */
} config_result_t;

config_result_t config_parse(config_t *cfg, int argc, char **argv);

#endif
