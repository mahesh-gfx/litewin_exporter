/* Windows-only collector (never a native unit module), so no _WIN32 guard.
 * Uses only the pdh_util wrappers, so it needs no windows.h itself. */
#include "collectors/system.h"
#include "pdh_util.h"

static void *c_procs, *c_threads, *c_exc;

int system_init(void)
{
    if (!pdh_open())
        return 0;
    c_procs = pdh_add_english(L"\\System\\Processes");
    c_threads = pdh_add_english(L"\\System\\Threads");
    c_exc = pdh_add_english(L"\\System\\Exception Dispatches/sec");
    return 1;
}

int collect_system(buf_t *b)
{
    long long v;
    int ok = 1;

    if (pdh_raw(c_procs, &v)) {
        tf_family(b, "windows_system_processes", "gauge", "Current number of processes.");
        tf_sample(b, "windows_system_processes", NULL, (double)v);
    } else {
        ok = 0;
    }
    if (pdh_raw(c_threads, &v)) {
        tf_family(b, "windows_system_threads", "gauge", "Current number of threads.");
        tf_sample(b, "windows_system_threads", NULL, (double)v);
    } else {
        ok = 0;
    }
    if (pdh_raw(c_exc, &v)) {
        tf_family(b, "windows_system_exception_dispatches_total", "counter",
                  "Total exceptions dispatched.");
        tf_sample(b, "windows_system_exception_dispatches_total", NULL, (double)v);
    } else {
        ok = 0;
    }
    return ok;
}
