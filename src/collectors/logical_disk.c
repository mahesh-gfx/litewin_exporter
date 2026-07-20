/* Windows-only collector (never a native unit module), so no _WIN32 guard. */
#include "collectors/logical_disk.h"
#include "pdh_util.h"
#include "strutil.h"

#include <windows.h>
#include <stdio.h>

static void *c_read_bytes, *c_write_bytes, *c_reads, *c_writes;

int logical_disk_init(void)
{
    if (!pdh_open())
        return 0;
    c_read_bytes = pdh_add_english(L"\\LogicalDisk(*)\\Disk Read Bytes/sec");
    c_write_bytes = pdh_add_english(L"\\LogicalDisk(*)\\Disk Write Bytes/sec");
    c_reads = pdh_add_english(L"\\LogicalDisk(*)\\Disk Reads/sec");
    c_writes = pdh_add_english(L"\\LogicalDisk(*)\\Disk Writes/sec");
    return 1;
}

/* Emit one gauge per fixed drive. want_free: 1 = free bytes, 0 = total size. */
static void disk_space(buf_t *b, const char *metric, DWORD mask, int want_free)
{
    int i;
    for (i = 0; i < 26; i++) {
        WCHAR root[8];
        ULARGE_INTEGER favail, total, ftotal;
        char lbl[16];
        if (!(mask & (1u << i)))
            continue;
        root[0] = (WCHAR)(L'A' + i);
        root[1] = L':';
        root[2] = L'\\';
        root[3] = 0;
        if (GetDriveTypeW(root) != DRIVE_FIXED)
            continue;
        if (!GetDiskFreeSpaceExW(root, &favail, &total, &ftotal))
            continue;
        snprintf(lbl, sizeof lbl, "volume=\"%c:\"", 'A' + i);
        tf_sample(b, metric, lbl,
                  want_free ? (double)ftotal.QuadPart : (double)total.QuadPart);
    }
}

struct disk_ctx {
    buf_t *b;
    const char *metric;
};

static void disk_cb(const char *inst, long long v, void *p)
{
    struct disk_ctx *c = (struct disk_ctx *)p;
    char vol[64], labels[96];
    lw_instance_volume(inst, vol, sizeof vol);
    snprintf(labels, sizeof labels, "volume=\"%s\"", vol);
    tf_sample(c->b, c->metric, labels, (double)v);
}

int collect_logical_disk(buf_t *b)
{
    DWORD mask = GetLogicalDrives();
    int i, ok = 1;
    struct {
        void *c;
        const char *name;
        const char *help;
    } io[4];

    io[0].c = c_read_bytes;  io[0].name = "windows_logical_disk_read_bytes_total";  io[0].help = "Bytes read from the disk.";
    io[1].c = c_write_bytes; io[1].name = "windows_logical_disk_write_bytes_total"; io[1].help = "Bytes written to the disk.";
    io[2].c = c_reads;       io[2].name = "windows_logical_disk_reads_total";       io[2].help = "Read operations on the disk.";
    io[3].c = c_writes;      io[3].name = "windows_logical_disk_writes_total";      io[3].help = "Write operations on the disk.";

    tf_family(b, "windows_logical_disk_free_bytes", "gauge", "Free space in bytes.");
    disk_space(b, "windows_logical_disk_free_bytes", mask, 1);
    tf_family(b, "windows_logical_disk_size_bytes", "gauge", "Total space in bytes.");
    disk_space(b, "windows_logical_disk_size_bytes", mask, 0);

    for (i = 0; i < 4; i++) {
        struct disk_ctx ctx;
        ctx.b = b;
        ctx.metric = io[i].name;
        tf_family(b, io[i].name, "counter", io[i].help);
        if (!pdh_raw_array(io[i].c, disk_cb, &ctx))
            ok = 0;
    }
    return ok;
}
