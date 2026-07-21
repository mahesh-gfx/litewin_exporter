#ifndef LITEWIN_COLLECTOR_NET_H
#define LITEWIN_COLLECTOR_NET_H

#include "textfmt.h" /* buf_t */

/* Pure: Current Bandwidth is reported in bits/sec -> bytes/sec. */
double net_bw_bits_to_bytes(long long bits_per_sec);

int net_init(void);          /* add the Network Interface counters */
int collect_net(buf_t *b);   /* per-nic bytes/packets, sanitised nic labels */

#endif
