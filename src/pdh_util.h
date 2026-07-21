#ifndef LITEWIN_PDH_UTIL_H
#define LITEWIN_PDH_UTIL_H

#include <wchar.h>

/* Minimal PDH raw-counter access, Windows only. Handles are opaque (void *) so
 * this header stays free of <pdh.h>; collectors add English (locale-proof)
 * counters at init and read raw cumulative values each scrape. */

int pdh_open(void);                          /* open the shared query; idempotent; 1/0 */
void *pdh_add_english(const wchar_t *path);  /* PdhAddEnglishCounterW; NULL on failure */
void pdh_collect(void);                      /* refresh all counters; call once per scrape */

/* Single-instance raw counter value; 1 ok, 0 fail. */
int pdh_raw(void *counter, long long *out);

/* Iterate a wildcard counter's instances (skipping "_Total"); 1 ok, 0 fail. */
typedef void (*pdh_inst_cb)(const char *instance, long long value, void *ctx);
int pdh_raw_array(void *counter, pdh_inst_cb cb, void *ctx);

#endif
