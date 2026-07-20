#include "registry.h"

#include <stdio.h>
#include <string.h>

collector_t *registry_collectors(size_t *n)
{
    /* No built-in collectors yet;
     * Callers already loop 0..n, so NULL/0 is safe. */
    *n = 0;
    return NULL;
}

int collectors_resolve(collector_t *cols, size_t n, const char *list,
                       char *unknown, size_t unknown_sz)
{
    char copy[512];
    char *tok, *save = NULL;
    size_t i;

    if (!list || !*list || strcmp(list, "[defaults]") == 0) {
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
