#include "pdh_util.h"
#include "log.h"

#include <windows.h>
#include <pdh.h>
#include <stdlib.h>
#include <string.h>

/* MinGW's pdh.h is sometimes missing these; define defensively. */
#define PDH_MORE_DATA_C    ((PDH_STATUS)0x800007D2L)
#define PDH_CSTATUS_OK_C    0x00000000
#define PDH_CSTATUS_NEWDATA 0x00000001

static PDH_HQUERY g_query;
static int g_open;

static void w2utf8(const WCHAR *w, char *out, int outsz)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, outsz, NULL, NULL);
    if (n <= 0 && outsz > 0)
        out[0] = '\0';
    out[outsz - 1] = '\0';
}

int pdh_open(void)
{
    if (g_open)
        return 1;
    if (PdhOpenQueryW(NULL, 0, &g_query) != 0) {
        log_msg("error: PdhOpenQueryW failed");
        return 0;
    }
    g_open = 1;
    return 1;
}

void *pdh_add_english(const wchar_t *path)
{
    PDH_HCOUNTER h = NULL;
    PDH_STATUS s;
    if (!g_open)
        return NULL;
    s = PdhAddEnglishCounterW(g_query, path, 0, &h);
    if (s != 0) {
        log_msg("warning: PdhAddEnglishCounterW(%ls) failed: 0x%lx", path, (unsigned long)s);
        return NULL;
    }
    return (void *)h;
}

void pdh_collect(void)
{
    if (g_open)
        PdhCollectQueryData(g_query);
}

int pdh_raw_array(void *counter, pdh_inst_cb cb, void *ctx)
{
    PDH_HCOUNTER c = (PDH_HCOUNTER)counter;
    DWORD bufsz = 0, count = 0, i;
    PDH_RAW_COUNTER_ITEM_W *items;
    PDH_STATUS s;

    if (!c)
        return 0;
    s = PdhGetRawCounterArrayW(c, &bufsz, &count, NULL);
    if (s == 0 && count == 0)
        return 1;
    if (s != PDH_MORE_DATA_C)
        return 0;

    items = (PDH_RAW_COUNTER_ITEM_W *)malloc(bufsz);
    if (!items)
        return 0;
    s = PdhGetRawCounterArrayW(c, &bufsz, &count, items);
    if (s != 0) {
        free(items);
        return 0;
    }

    for (i = 0; i < count; i++) {
        char name[512];
        if (items[i].RawValue.CStatus != PDH_CSTATUS_OK_C &&
            items[i].RawValue.CStatus != PDH_CSTATUS_NEWDATA)
            continue;
        w2utf8(items[i].szName, name, sizeof name);
        if (strcmp(name, "_Total") == 0)
            continue;
        cb(name, (long long)items[i].RawValue.FirstValue, ctx);
    }
    free(items);
    return 1;
}
