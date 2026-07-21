#include "collectors/net.h"

double net_bw_bits_to_bytes(long long bits_per_sec)
{
    return (double)bits_per_sec / 8.0;
}

#ifdef _WIN32
#include "pdh_util.h"
#include "strutil.h"

#include <stdio.h>

/* Metadata is constant; handles (same order) are filled in at init. */
static const struct {
    const char *name;
    const char *typ;
    const char *help;
    int bits_to_bytes;
} NET_METRICS[8] = {
    { "windows_net_bytes_sent_total", "counter", "Bytes sent by the interface.", 0 },
    { "windows_net_bytes_received_total", "counter", "Bytes received by the interface.", 0 },
    { "windows_net_bytes_total", "counter", "Bytes sent and received by the interface.", 0 },
    { "windows_net_current_bandwidth_bytes", "gauge", "Interface bandwidth estimate in bytes per second.", 1 },
    { "windows_net_packets_outbound_discarded_total", "counter", "Outbound packets discarded.", 0 },
    { "windows_net_packets_outbound_errors_total", "counter", "Outbound packets with errors.", 0 },
    { "windows_net_packets_received_discarded_total", "counter", "Received packets discarded.", 0 },
    { "windows_net_packets_received_errors_total", "counter", "Received packets with errors.", 0 },
};

static void *c_net[8];

int net_init(void)
{
    if (!pdh_open())
        return 0;
    c_net[0] = pdh_add_english(L"\\Network Interface(*)\\Bytes Sent/sec");
    c_net[1] = pdh_add_english(L"\\Network Interface(*)\\Bytes Received/sec");
    c_net[2] = pdh_add_english(L"\\Network Interface(*)\\Bytes Total/sec");
    c_net[3] = pdh_add_english(L"\\Network Interface(*)\\Current Bandwidth");
    c_net[4] = pdh_add_english(L"\\Network Interface(*)\\Packets Outbound Discarded");
    c_net[5] = pdh_add_english(L"\\Network Interface(*)\\Packets Outbound Errors");
    c_net[6] = pdh_add_english(L"\\Network Interface(*)\\Packets Received Discarded");
    c_net[7] = pdh_add_english(L"\\Network Interface(*)\\Packets Received Errors");
    return 1;
}

struct net_ctx {
    buf_t *b;
    const char *metric;
    int bits_to_bytes;
};

static void net_cb(const char *inst, long long v, void *p)
{
    struct net_ctx *c = (struct net_ctx *)p;
    char nic[256], labels[300];
    lw_sanitize_nic(inst, nic, sizeof nic);
    snprintf(labels, sizeof labels, "nic=\"%s\"", nic);
    tf_sample(c->b, c->metric, labels,
              c->bits_to_bytes ? net_bw_bits_to_bytes(v) : (double)v);
}

int collect_net(buf_t *b)
{
    int i, ok = 1;
    for (i = 0; i < 8; i++) {
        struct net_ctx ctx;
        ctx.b = b;
        ctx.metric = NET_METRICS[i].name;
        ctx.bits_to_bytes = NET_METRICS[i].bits_to_bytes;
        tf_family(b, NET_METRICS[i].name, NET_METRICS[i].typ, NET_METRICS[i].help);
        if (!pdh_raw_array(c_net[i], net_cb, &ctx))
            ok = 0;
    }
    return ok;
}
#endif
