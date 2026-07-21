#ifndef LITEWIN_COLLECTOR_DISK_HEALTH_H
#define LITEWIN_COLLECTOR_DISK_HEALTH_H

#include "textfmt.h" /* buf_t */

/* SMART failure prediction per physical drive. Windows only; needs elevation
 * to open the drives (otherwise emits nothing and reports failure). */
int collect_disk_health(buf_t *b);

#endif
