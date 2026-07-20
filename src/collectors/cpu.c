#include "collectors/cpu.h"

#include <stdio.h>

double cpu_ticks_to_seconds(long long ticks_100ns)
{
    return (double)ticks_100ns * 1e-7;
}

void cpu_time_sample(buf_t *b, const char *core, const char *mode,
                     long long ticks_100ns)
{
    char labels[64];
    snprintf(labels, sizeof labels, "core=\"0,%s\",mode=\"%s\"", core, mode);
    tf_sample(b, "windows_cpu_time_total", labels, cpu_ticks_to_seconds(ticks_100ns));
}

#ifdef _WIN32
#include <windows.h>
#include "pdh_util.h"

/* NULL when unavailable on this system (e.g. under Wine). */
static void *c_idle, *c_user, *c_priv, *c_intr, *c_dpc;

int cpu_init(void)
{
    if (!pdh_open())
        return 0;
    c_idle = pdh_add_english(L"\\Processor(*)\\% Idle Time");
    c_user = pdh_add_english(L"\\Processor(*)\\% User Time");
    c_priv = pdh_add_english(L"\\Processor(*)\\% Privileged Time");
    c_intr = pdh_add_english(L"\\Processor(*)\\% Interrupt Time");
    c_dpc = pdh_add_english(L"\\Processor(*)\\% DPC Time");
    return 1;
}

struct cpu_cb_ctx {
    buf_t *b;
    const char *mode;
};

static void cpu_cb(const char *inst, long long v, void *p)
{
    struct cpu_cb_ctx *c = (struct cpu_cb_ctx *)p;
    cpu_time_sample(c->b, inst, c->mode, v); /* Processor instance name is the core index */
}

int collect_cpu(buf_t *b)
{
    SYSTEM_INFO si;
    int ok = 1, i;
    struct {
        void *c;
        const char *mode;
    } t[5];

    t[0].c = c_idle; t[0].mode = "idle";
    t[1].c = c_user; t[1].mode = "user";
    t[2].c = c_priv; t[2].mode = "privileged";
    t[3].c = c_intr; t[3].mode = "interrupt";
    t[4].c = c_dpc;  t[4].mode = "dpc";

    tf_family(b, "windows_cpu_time_total", "counter",
              "Time the processor spent in each mode, in seconds.");
    for (i = 0; i < 5; i++) {
        struct cpu_cb_ctx ctx;
        ctx.b = b;
        ctx.mode = t[i].mode;
        if (!pdh_raw_array(t[i].c, cpu_cb, &ctx))
            ok = 0;
    }

    GetSystemInfo(&si);
    tf_family(b, "windows_cpu_core_frequency_mhz", "gauge",
              "Nominal core frequency in megahertz.");
    for (i = 0; i < (int)si.dwNumberOfProcessors; i++) {
        WCHAR key[128];
        DWORD mhz = 0, sz = sizeof mhz, type;
        HKEY hk;
        _snwprintf(key, 127, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\%d", i);
        key[127] = 0;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &hk) == ERROR_SUCCESS) {
            if (RegQueryValueExW(hk, L"~MHz", NULL, &type, (LPBYTE)&mhz, &sz) == ERROR_SUCCESS) {
                char lbl[32];
                snprintf(lbl, sizeof lbl, "core=\"0,%d\"", i);
                tf_sample(b, "windows_cpu_core_frequency_mhz", lbl, (double)mhz);
            }
            RegCloseKey(hk);
        }
    }

    tf_family(b, "windows_cpu_logical_processor", "gauge",
              "Number of installed logical processors.");
    tf_sample(b, "windows_cpu_logical_processor", NULL, (double)si.dwNumberOfProcessors);
    return ok;
}
#endif
