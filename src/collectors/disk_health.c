/* Windows-only collector (never a native unit module), so no _WIN32 guard.
 * SMART can't be faked, so there is no unit test — tier-5 only (roadmap M12). */
#include "collectors/disk_health.h"

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <string.h>

#define MAX_DRIVES 16

#ifndef IOCTL_STORAGE_PREDICT_FAILURE
#define IOCTL_STORAGE_PREDICT_FAILURE \
    CTL_CODE(IOCTL_STORAGE_BASE, 0x0440, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

typedef struct {
    ULONG PredictFailure; /* nonzero => drive predicts imminent failure */
    UCHAR VendorSpecific[512];
} STORAGE_PREDICT_FAILURE_T;

/* 1 = queried ok (*failing set), 0 = drive absent/unsupported. */
static int query_disk_health(int drive_index, int *failing)
{
    WCHAR path[64];
    HANDLE h;
    STORAGE_PREDICT_FAILURE_T spf;
    DWORD ret = 0;
    BOOL ok;
    DWORD access[2] = { 0, GENERIC_READ }; /* zero-access first, then read-only; never write */
    int a;

    _snwprintf(path, 63, L"\\\\.\\PhysicalDrive%d", drive_index);
    path[63] = 0;

    h = INVALID_HANDLE_VALUE;
    for (a = 0; a < 2 && h == INVALID_HANDLE_VALUE; a++)
        h = CreateFileW(path, access[a], FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    memset(&spf, 0, sizeof spf);
    ok = DeviceIoControl(h, IOCTL_STORAGE_PREDICT_FAILURE,
                         NULL, 0, &spf, sizeof spf, &ret, NULL);
    CloseHandle(h);
    if (!ok)
        return 0;
    *failing = (spf.PredictFailure != 0);
    return 1;
}

int collect_disk_health(buf_t *b)
{
    int i, any = 0;
    tf_family(b, "windows_physical_disk_failure_predicted", "gauge",
              "SMART failure prediction (1 = imminent failure predicted, 0 = healthy).");
    for (i = 0; i < MAX_DRIVES; i++) {
        int failing = 0;
        if (query_disk_health(i, &failing)) {
            char lbl[24];
            snprintf(lbl, sizeof lbl, "disk=\"%d\"", i);
            tf_sample(b, "windows_physical_disk_failure_predicted", lbl, failing);
            any = 1;
        }
    }
    return any; /* 0 when no drive answered (e.g. non-elevated) -> collector_success 0 */
}
