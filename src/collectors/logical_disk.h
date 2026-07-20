#ifndef LITEWIN_COLLECTOR_LOGICAL_DISK_H
#define LITEWIN_COLLECTOR_LOGICAL_DISK_H

#include "textfmt.h" /* buf_t */

int logical_disk_init(void);      /* add the LogicalDisk IO counters */
int collect_logical_disk(buf_t *b); /* free/size (GetDiskFreeSpaceEx) + IO (PDH) */

#endif
