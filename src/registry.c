#include "registry.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include "collectors/cpu.h"
#include "collectors/disk_health.h"
#include "collectors/logical_disk.h"
#include "collectors/memory.h"
#include "collectors/net.h"
#include "collectors/service.h"
#include "collectors/system.h"

/* Built-in collectors. init is optional (NULL). Windows only: the collect
 * functions call windows.h, so native builds (unit tests) see an empty set. */
static collector_t g_collectors[] = {
    { "cpu", cpu_init, collect_cpu, 0 },
    { "disk_health", NULL, collect_disk_health, 0 },
    { "logical_disk", logical_disk_init, collect_logical_disk, 0 },
    { "memory", NULL, collect_memory, 0 },
    { "net", net_init, collect_net, 0 },
    { "service", NULL, collect_service, 0 },
    { "system", system_init, collect_system, 0 },
};

collector_t *registry_collectors(size_t *n)
{
    *n = sizeof g_collectors / sizeof g_collectors[0];
    return g_collectors;
}
#else
collector_t *registry_collectors(size_t *n)
{
    *n = 0;
    return NULL; /* no windows.h collectors when built natively */
}
#endif

int collectors_resolve(collector_t *cols, size_t n, const char *list,
                       char *unknown, size_t unknown_sz)
{
    char copy[512];
    char *tok, *save = NULL;
    size_t i;

    if (!list || !*list) { /* a "[defaults]" token is handled in the loop */
        for (i = 0; i < n; i++)
            cols[i].enabled = 1;
        return 1;
    }

    for (i = 0; i < n; i++)
        cols[i].enabled = 0;

    strncpy(copy, list, sizeof copy - 1);
    copy[sizeof copy - 1] = '\0';

    for (tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        int found = 0;
        while (*tok == ' ')
            tok++;
        if (strcmp(tok, "[defaults]") == 0) {
            for (i = 0; i < n; i++)
                cols[i].enabled = 1;
            continue;
        }
        for (i = 0; i < n; i++) {
            if (strcmp(tok, cols[i].name) == 0) {
                cols[i].enabled = 1;
                found = 1;
                break;
            }
        }
        if (!found) {
            strncpy(unknown, tok, unknown_sz - 1);
            unknown[unknown_sz - 1] = '\0';
            return 0;
        }
    }
    return 1;
}

void collectors_print(collector_t *cols, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        printf("%s\n", cols[i].name);
}
