/*
 * litewin_exporter - minimal Prometheus exporter for Windows 7+ low-spec hosts.
 *
 * Exposes the windows_exporter-compatible metric names used by the
 * "JWNC Windows Hosts" Grafana dashboard, plus a SMART disk-health metric.
 *
 * Design constraints:
 *   - No WMI (heavy), no third-party libraries, no runtime dependencies.
 *   - Single static binary built with maintained MinGW-w64 GCC.
 *   - Data sources: Win32 APIs + PDH raw counters (locale-independent via
 *     PdhAddEnglishCounterW) + IOCTL_STORAGE_PREDICT_FAILURE for disk health.
 *
 * Build (cross, from Linux):
 *   x86_64-w64-mingw32-gcc -O2 -s -static -D_WIN32_WINNT=0x0601 \
 *       -o litewin_exporter_amd64.exe litewin_exporter.c -lws2_32 -ladvapi32
 *   i686-w64-mingw32-gcc  -O2 -s -static -D_WIN32_WINNT=0x0601 \
 *       -o litewin_exporter_386.exe   litewin_exporter.c -lws2_32 -ladvapi32
 */

 #ifndef _WIN32_WINNT
 #define _WIN32_WINNT 0x0601
 #endif
 #define WIN32_LEAN_AND_MEAN
 
 #include <winsock2.h>
 #include <windows.h>
 #include <winsvc.h>
 #include <winioctl.h>
 #include <pdh.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <stdarg.h>
 #include <ctype.h>
 
 #define EXPORTER_NAME    "litewin_exporter"
 #define EXPORTER_VERSION "0.4.0"
 #define DEFAULT_PORT     9182
 #define MAX_DRIVES       16   /* PhysicalDrive0..15 probed for SMART */
 
 /* ------------------------------------------------------------------ */
 /* growable output buffer                                              */
 /* ------------------------------------------------------------------ */
 
 typedef struct {
     char  *p;
     size_t len;
     size_t cap;
 } buf_t;
 
 static void buf_init(buf_t *b) {
     b->cap = 64 * 1024;
     b->len = 0;
     b->p = (char *)malloc(b->cap);
     if (b->p) b->p[0] = '\0';
 }
 
 static void buf_free(buf_t *b) { free(b->p); b->p = NULL; b->len = b->cap = 0; }
 
 static void buf_appendf(buf_t *b, const char *fmt, ...) {
     va_list ap;
     int n;
     if (!b->p) return;
     for (;;) {
         va_start(ap, fmt);
         n = vsnprintf(b->p + b->len, b->cap - b->len, fmt, ap);
         va_end(ap);
         if (n < 0) return;
         if ((size_t)n < b->cap - b->len) { b->len += (size_t)n; return; }
         {
             size_t ncap = b->cap * 2;
             char *np;
             while (ncap - b->len <= (size_t)n) ncap *= 2;
             np = (char *)realloc(b->p, ncap);
             if (!np) return;
             b->p = np;
             b->cap = ncap;
         }
     }
 }
 
 /* UTF-16 -> UTF-8 into a caller buffer; always NUL-terminates. */
 static void w2utf8(const WCHAR *w, char *out, int outsz) {
     int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, outsz, NULL, NULL);
     if (n <= 0 && outsz > 0) out[0] = '\0';
     out[outsz - 1] = '\0';
 }
 
 /* escape a label value per the Prometheus exposition format */
 static void escape_label(const char *in, char *out, int outsz) {
     int j = 0, i;
     for (i = 0; in[i] && j < outsz - 3; i++) {
         char c = in[i];
         if (c == '\\' || c == '"') { out[j++] = '\\'; out[j++] = c; }
         else if (c == '\n')        { out[j++] = '\\'; out[j++] = 'n'; }
         else                        out[j++] = c;
     }
     out[j] = '\0';
 }
 
 /* replace anything outside [A-Za-z0-9] with '_' (windows_exporter NIC style) */
 static void sanitize_nic(const char *in, char *out, int outsz) {
     int i;
     for (i = 0; in[i] && i < outsz - 1; i++) {
         char c = in[i];
         out[i] = (isalnum((unsigned char)c)) ? c : '_';
     }
     out[i] = '\0';
 }
 
 static void log_msg(const char *fmt, ...) {
     va_list ap;
     va_start(ap, fmt);
     vfprintf(stderr, fmt, ap);
     fputc('\n', stderr);
     va_end(ap);
 }
 
 /* ------------------------------------------------------------------ */
 /* PDH: dynamically loaded, raw (cumulative) counter access            */
 /* ------------------------------------------------------------------ */
 
 #define PDH_MORE_DATA_C     ((PDH_STATUS)0x800007D2L)
 #define PDH_CSTATUS_OK_C     0x00000000
 #define PDH_CSTATUS_NEWDATA  0x00000001
 
 typedef PDH_HQUERY   PDH_HQUERY_T;
 typedef PDH_HCOUNTER PDH_HCOUNTER_T;
 
 static PDH_HQUERY_T g_query;
 
 /* counter handles (NULL when unavailable on this system) */
 static PDH_HCOUNTER_T c_cpu_idle, c_cpu_user, c_cpu_priv, c_cpu_intr, c_cpu_dpc;
 static PDH_HCOUNTER_T c_disk_read_bytes, c_disk_write_bytes, c_disk_reads, c_disk_writes;
 static PDH_HCOUNTER_T c_net_sent, c_net_recv, c_net_total, c_net_bw;
 static PDH_HCOUNTER_T c_net_out_disc, c_net_out_err, c_net_in_disc, c_net_in_err;
 static PDH_HCOUNTER_T c_sys_procs, c_sys_threads, c_sys_exceptions;
 
 static PDH_HCOUNTER_T pdh_add(const WCHAR *path) {
     PDH_HCOUNTER_T h = NULL;
     PDH_STATUS s = PdhAddEnglishCounterW(g_query, path, 0, &h);
     if (s != 0) {
         log_msg("warning: PdhAddEnglishCounterW(%ls) failed: 0x%lx", path, (unsigned long)s);
         return NULL;
     }
     return h;
 }
 
 static int pdh_init(int want_cpu, int want_disk, int want_net, int want_sys) {
     if (PdhOpenQueryW(NULL, 0, &g_query) != 0) {
         log_msg("error: PdhOpenQueryW failed");
         return 0;
     }
 
     if (want_cpu) {
         c_cpu_idle = pdh_add(L"\\Processor(*)\\% Idle Time");
         c_cpu_user = pdh_add(L"\\Processor(*)\\% User Time");
         c_cpu_priv = pdh_add(L"\\Processor(*)\\% Privileged Time");
         c_cpu_intr = pdh_add(L"\\Processor(*)\\% Interrupt Time");
         c_cpu_dpc  = pdh_add(L"\\Processor(*)\\% DPC Time");
     }
 
     if (want_disk) {
         c_disk_read_bytes  = pdh_add(L"\\LogicalDisk(*)\\Disk Read Bytes/sec");
         c_disk_write_bytes = pdh_add(L"\\LogicalDisk(*)\\Disk Write Bytes/sec");
         c_disk_reads       = pdh_add(L"\\LogicalDisk(*)\\Disk Reads/sec");
         c_disk_writes      = pdh_add(L"\\LogicalDisk(*)\\Disk Writes/sec");
     }
     if (want_net) {
         c_net_sent     = pdh_add(L"\\Network Interface(*)\\Bytes Sent/sec");
         c_net_recv     = pdh_add(L"\\Network Interface(*)\\Bytes Received/sec");
         c_net_total    = pdh_add(L"\\Network Interface(*)\\Bytes Total/sec");
         c_net_bw       = pdh_add(L"\\Network Interface(*)\\Current Bandwidth");
         c_net_out_disc = pdh_add(L"\\Network Interface(*)\\Packets Outbound Discarded");
         c_net_out_err  = pdh_add(L"\\Network Interface(*)\\Packets Outbound Errors");
         c_net_in_disc  = pdh_add(L"\\Network Interface(*)\\Packets Received Discarded");
         c_net_in_err   = pdh_add(L"\\Network Interface(*)\\Packets Received Errors");
     }
     if (want_sys) {
         c_sys_procs      = pdh_add(L"\\System\\Processes");
         c_sys_threads    = pdh_add(L"\\System\\Threads");
         c_sys_exceptions = pdh_add(L"\\System\\Exception Dispatches/sec");
     }
 
     /* prime once so the first scrape already returns data */
     PdhCollectQueryData(g_query);
     return 1;
 }
 
 /* single-instance raw value; returns 1 on success */
 static int pdh_raw(PDH_HCOUNTER_T c, LONGLONG *out) {
     DWORD type;
     PDH_RAW_COUNTER rc;
     if (!c) return 0;
     if (PdhGetRawCounterValue(c, &type, &rc) != 0) return 0;
     if (rc.CStatus != PDH_CSTATUS_OK_C && rc.CStatus != PDH_CSTATUS_NEWDATA) return 0;
     *out = rc.FirstValue;
     return 1;
 }
 
 /*
  * Iterate every instance of a wildcard counter, invoking cb(name, value, ctx).
  * Returns 1 on success, 0 on failure.
  */
 typedef void (*inst_cb)(const char *instance, LONGLONG value, void *ctx);
 
 static int pdh_raw_array(PDH_HCOUNTER_T c, inst_cb cb, void *ctx) {
     DWORD bufsz = 0, count = 0;
     PDH_RAW_COUNTER_ITEM_W *items;
     PDH_STATUS s;
     DWORD i;
 
     if (!c) return 0;
     s = PdhGetRawCounterArrayW(c, &bufsz, &count, NULL);
     if (s == 0 && count == 0) return 1;
     if (s != PDH_MORE_DATA_C) return 0;
 
     items = (PDH_RAW_COUNTER_ITEM_W *)malloc(bufsz);
     if (!items) return 0;
     s = PdhGetRawCounterArrayW(c, &bufsz, &count, items);
     if (s != 0) { free(items); return 0; }
 
     for (i = 0; i < count; i++) {
         char name[512];
         if (items[i].RawValue.CStatus != PDH_CSTATUS_OK_C &&
             items[i].RawValue.CStatus != PDH_CSTATUS_NEWDATA)
             continue;
         w2utf8(items[i].szName, name, sizeof name);
         if (strcmp(name, "_Total") == 0) continue;
         cb(name, items[i].RawValue.FirstValue, ctx);
     }
     free(items);
     return 1;
 }
 
 /* ------------------------------------------------------------------ */
 /* disk health: IOCTL_STORAGE_PREDICT_FAILURE (SMART, no WMI)          */
 /* ------------------------------------------------------------------ */
 
 #ifndef IOCTL_STORAGE_PREDICT_FAILURE
 #define IOCTL_STORAGE_PREDICT_FAILURE \
     CTL_CODE(IOCTL_STORAGE_BASE, 0x0440, METHOD_BUFFERED, FILE_ANY_ACCESS)
 #endif
 
 typedef struct {
     ULONG PredictFailure;        /* nonzero => drive predicts imminent failure */
     UCHAR VendorSpecific[512];
 } STORAGE_PREDICT_FAILURE_T;
 
 /* returns: 1 = queried ok (*failing set), 0 = drive absent/unsupported */
 static int query_disk_health(int drive_index, int *failing) {
     WCHAR path[64];
     HANDLE h;
     STORAGE_PREDICT_FAILURE_T spf;
     DWORD ret = 0;
     BOOL ok;
     DWORD access[2] = { 0, GENERIC_READ };   /* query-only first; never write */
     int a;
 
     _snwprintf(path, 63, L"\\\\.\\PhysicalDrive%d", drive_index);
     path[63] = 0;
 
     h = INVALID_HANDLE_VALUE;
     for (a = 0; a < 2 && h == INVALID_HANDLE_VALUE; a++) {
         h = CreateFileW(path, access[a], FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, 0, NULL);
     }
     if (h == INVALID_HANDLE_VALUE) return 0;
 
     memset(&spf, 0, sizeof spf);
     ok = DeviceIoControl(h, IOCTL_STORAGE_PREDICT_FAILURE,
                          NULL, 0, &spf, sizeof spf, &ret, NULL);
     CloseHandle(h);
     if (!ok) return 0;
     *failing = (spf.PredictFailure != 0);
     return 1;
 }
 
 /* ------------------------------------------------------------------ */
 /* collectors                                                          */
 /* ------------------------------------------------------------------ */
 
 static double g_start_time;   /* unix seconds, set once at startup */
 static char   g_metrics_path[128] = "/metrics";
 
 static double filetime_unix_now(void) {
     FILETIME ft;
     ULARGE_INTEGER u;
     GetSystemTimeAsFileTime(&ft);
     u.LowPart = ft.dwLowDateTime;
     u.HighPart = ft.dwHighDateTime;
     return ((double)u.QuadPart - 116444736000000000.0) / 1e7;
 }
 
 struct cpu_ctx { buf_t *b; const char *mode; };
 
 static void cpu_cb(const char *inst, LONGLONG v, void *ctxp) {
     struct cpu_ctx *ctx = (struct cpu_ctx *)ctxp;
     /* Processor object instances are the core index: "0", "1", ...
        Emit windows_exporter's "socket,core" label format. */
     buf_appendf(ctx->b, "windows_cpu_time_total{core=\"0,%s\",mode=\"%s\"} %.6f\n",
                 inst, ctx->mode, (double)v * 1e-7);
 }
 
 static int collect_cpu(buf_t *b) {
     SYSTEM_INFO si;
     HKEY hk;
     int i, ok = 1;
     struct { PDH_HCOUNTER_T c; const char *mode; } t[5] = {
         { NULL, "idle" }, { NULL, "user" }, { NULL, "privileged" },
         { NULL, "interrupt" }, { NULL, "dpc" },
     };
     t[0].c = c_cpu_idle; t[1].c = c_cpu_user; t[2].c = c_cpu_priv;
     t[3].c = c_cpu_intr; t[4].c = c_cpu_dpc;
 
     GetSystemInfo(&si);
 
     buf_appendf(b,
         "# HELP windows_cpu_time_total Time that processor spent in different modes (idle, user, privileged, interrupt, dpc)\n"
         "# TYPE windows_cpu_time_total counter\n");
     for (i = 0; i < 5; i++) {
         struct cpu_ctx ctx;
         ctx.b = b; ctx.mode = t[i].mode;
         if (!pdh_raw_array(t[i].c, cpu_cb, &ctx)) ok = 0;
     }
 
     buf_appendf(b,
         "# HELP windows_cpu_core_frequency_mhz Core frequency in megahertz\n"
         "# TYPE windows_cpu_core_frequency_mhz gauge\n");
     for (i = 0; i < (int)si.dwNumberOfProcessors; i++) {
         WCHAR key[128];
         DWORD mhz = 0, sz = sizeof mhz, type;
         _snwprintf(key, 127, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\%d", i);
         key[127] = 0;
         if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &hk) == ERROR_SUCCESS) {
             if (RegQueryValueExW(hk, L"~MHz", NULL, &type, (LPBYTE)&mhz, &sz) == ERROR_SUCCESS)
                 buf_appendf(b, "windows_cpu_core_frequency_mhz{core=\"0,%d\"} %lu\n", i, (unsigned long)mhz);
             RegCloseKey(hk);
         }
     }
 
     buf_appendf(b,
         "# HELP windows_cpu_logical_processor Number of installed logical processors\n"
         "# TYPE windows_cpu_logical_processor gauge\n"
         "windows_cpu_logical_processor %lu\n", (unsigned long)si.dwNumberOfProcessors);
     return ok;
 }
 
 static int collect_memory(buf_t *b) {
     MEMORYSTATUSEX ms;
     ms.dwLength = sizeof ms;
     if (!GlobalMemoryStatusEx(&ms)) return 0;
     buf_appendf(b,
         "# HELP windows_memory_physical_total_bytes The amount of actual physical memory in bytes\n"
         "# TYPE windows_memory_physical_total_bytes gauge\n"
         "windows_memory_physical_total_bytes %llu\n"
         "# HELP windows_memory_physical_free_bytes The amount of physical memory currently available in bytes\n"
         "# TYPE windows_memory_physical_free_bytes gauge\n"
         "windows_memory_physical_free_bytes %llu\n"
         "# HELP windows_memory_commit_limit Amount of virtual memory in bytes that can be committed without extending the paging file(s)\n"
         "# TYPE windows_memory_commit_limit gauge\n"
         "windows_memory_commit_limit %llu\n",
         (unsigned long long)ms.ullTotalPhys,
         (unsigned long long)ms.ullAvailPhys,
         (unsigned long long)ms.ullTotalPageFile);
     return 1;
 }
 
 /* perf instances look like "C:" or "0 C:" or "1 D: E:"; keep the last token */
 static void instance_volume(const char *inst, char *out, int outsz) {
     const char *sp = strrchr(inst, ' ');
     const char *v = sp ? sp + 1 : inst;
     escape_label(v, out, outsz);
 }
 
 struct disk_ctx { buf_t *b; const char *metric; };
 
 static void disk_cb(const char *inst, LONGLONG v, void *ctxp) {
     struct disk_ctx *ctx = (struct disk_ctx *)ctxp;
     char vol[256];
     instance_volume(inst, vol, sizeof vol);
     buf_appendf(ctx->b, "%s{volume=\"%s\"} %lld\n", ctx->metric, vol, (long long)v);
 }
 
 static int collect_logical_disk(buf_t *b) {
     DWORD mask = GetLogicalDrives();
     int i, ok = 1;
     struct { PDH_HCOUNTER_T c; const char *name; const char *help; } io[4] = {
         { NULL, "windows_logical_disk_read_bytes_total",  "The number of bytes transferred from the disk during read operations" },
         { NULL, "windows_logical_disk_write_bytes_total", "The number of bytes transferred to the disk during write operations" },
         { NULL, "windows_logical_disk_reads_total",       "The number of read operations on the disk" },
         { NULL, "windows_logical_disk_writes_total",      "The number of write operations on the disk" },
     };
     io[0].c = c_disk_read_bytes; io[1].c = c_disk_write_bytes;
     io[2].c = c_disk_reads;      io[3].c = c_disk_writes;
 
     buf_appendf(b,
         "# HELP windows_logical_disk_free_bytes Free space in bytes\n"
         "# TYPE windows_logical_disk_free_bytes gauge\n");
     for (i = 0; i < 26; i++) {
         WCHAR root[8];
         ULARGE_INTEGER favail, total, ftotal;
         if (!(mask & (1u << i))) continue;
         root[0] = (WCHAR)(L'A' + i); root[1] = L':'; root[2] = L'\\'; root[3] = 0;
         if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
         if (GetDiskFreeSpaceExW(root, &favail, &total, &ftotal))
             buf_appendf(b, "windows_logical_disk_free_bytes{volume=\"%c:\"} %llu\n",
                         'A' + i, (unsigned long long)ftotal.QuadPart);
     }
     buf_appendf(b,
         "# HELP windows_logical_disk_size_bytes Total space in bytes\n"
         "# TYPE windows_logical_disk_size_bytes gauge\n");
     for (i = 0; i < 26; i++) {
         WCHAR root[8];
         ULARGE_INTEGER favail, total, ftotal;
         if (!(mask & (1u << i))) continue;
         root[0] = (WCHAR)(L'A' + i); root[1] = L':'; root[2] = L'\\'; root[3] = 0;
         if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
         if (GetDiskFreeSpaceExW(root, &favail, &total, &ftotal))
             buf_appendf(b, "windows_logical_disk_size_bytes{volume=\"%c:\"} %llu\n",
                         'A' + i, (unsigned long long)total.QuadPart);
     }
 
     for (i = 0; i < 4; i++) {
         struct disk_ctx ctx;
         buf_appendf(b, "# HELP %s %s\n# TYPE %s counter\n", io[i].name, io[i].help, io[i].name);
         ctx.b = b; ctx.metric = io[i].name;
         if (!pdh_raw_array(io[i].c, disk_cb, &ctx)) ok = 0;
     }
     return ok;
 }
 
 struct net_ctx { buf_t *b; const char *metric; int bits_to_bytes; };
 
 static void net_cb(const char *inst, LONGLONG v, void *ctxp) {
     struct net_ctx *ctx = (struct net_ctx *)ctxp;
     char nic[256];
     sanitize_nic(inst, nic, sizeof nic);
     if (ctx->bits_to_bytes)
         buf_appendf(ctx->b, "%s{nic=\"%s\"} %.0f\n", ctx->metric, nic, (double)v / 8.0);
     else
         buf_appendf(ctx->b, "%s{nic=\"%s\"} %lld\n", ctx->metric, nic, (long long)v);
 }
 
 static int collect_net(buf_t *b) {
     int i, ok = 1;
     struct { PDH_HCOUNTER_T c; const char *name; const char *typ; const char *help; int b2b; } m[8] = {
         { NULL, "windows_net_bytes_sent_total", "counter", "Total bytes sent by interface", 0 },
         { NULL, "windows_net_bytes_received_total", "counter", "Total bytes received by interface", 0 },
         { NULL, "windows_net_bytes_total", "counter", "Total bytes sent and received by interface", 0 },
         { NULL, "windows_net_current_bandwidth_bytes", "gauge", "Estimate of the interface's current bandwidth in bytes per second", 1 },
         { NULL, "windows_net_packets_outbound_discarded_total", "counter", "Total outbound packets discarded", 0 },
         { NULL, "windows_net_packets_outbound_errors_total", "counter", "Total outbound packets with errors", 0 },
         { NULL, "windows_net_packets_received_discarded_total", "counter", "Total received packets discarded", 0 },
         { NULL, "windows_net_packets_received_errors_total", "counter", "Total received packets with errors", 0 },
     };
     m[0].c = c_net_sent;     m[1].c = c_net_recv;    m[2].c = c_net_total;   m[3].c = c_net_bw;
     m[4].c = c_net_out_disc; m[5].c = c_net_out_err; m[6].c = c_net_in_disc; m[7].c = c_net_in_err;
 
     for (i = 0; i < 8; i++) {
         struct net_ctx ctx;
         buf_appendf(b, "# HELP %s %s\n# TYPE %s %s\n", m[i].name, m[i].help, m[i].name, m[i].typ);
         ctx.b = b; ctx.metric = m[i].name; ctx.bits_to_bytes = m[i].b2b;
         if (!pdh_raw_array(m[i].c, net_cb, &ctx)) ok = 0;
     }
     return ok;
 }
 
 static int collect_system(buf_t *b) {
     LONGLONG v;
     int ok = 1;
     if (pdh_raw(c_sys_procs, &v))
         buf_appendf(b, "# HELP windows_system_processes Current number of processes\n"
                        "# TYPE windows_system_processes gauge\n"
                        "windows_system_processes %lld\n", (long long)v);
     else ok = 0;
     if (pdh_raw(c_sys_threads, &v))
         buf_appendf(b, "# HELP windows_system_threads Current number of threads\n"
                        "# TYPE windows_system_threads gauge\n"
                        "windows_system_threads %lld\n", (long long)v);
     else ok = 0;
     if (pdh_raw(c_sys_exceptions, &v))
         buf_appendf(b, "# HELP windows_system_exception_dispatches_total Total exceptions dispatched\n"
                        "# TYPE windows_system_exception_dispatches_total counter\n"
                        "windows_system_exception_dispatches_total %lld\n", (long long)v);
     else ok = 0;
     return ok;
 }
 
 static const char *service_state_name(DWORD s) {
     switch (s) {
         case SERVICE_STOPPED:          return "stopped";
         case SERVICE_START_PENDING:    return "start pending";
         case SERVICE_STOP_PENDING:     return "stop pending";
         case SERVICE_RUNNING:          return "running";
         case SERVICE_CONTINUE_PENDING: return "continue pending";
         case SERVICE_PAUSE_PENDING:    return "pause pending";
         case SERVICE_PAUSED:           return "paused";
         default:                       return "unknown";
     }
 }
 
 static const char *all_states[7] = {
     "continue pending", "pause pending", "paused", "running",
     "start pending", "stop pending", "stopped"
 };
 
 static int collect_services(buf_t *b) {
     SC_HANDLE scm;
     BYTE *buf = NULL;
     DWORD bufsz = 256 * 1024, needed = 0, count = 0, resume = 0;
     int ok = 1;
 
     scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
     if (!scm) return 0;
     buf = (BYTE *)malloc(bufsz);
     if (!buf) { CloseServiceHandle(scm); return 0; }
 
     buf_appendf(b, "# HELP windows_service_state The state of the service (State)\n"
                    "# TYPE windows_service_state gauge\n");
     for (;;) {
         BOOL r = EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                                        SERVICE_STATE_ALL, buf, bufsz,
                                        &needed, &count, &resume, NULL);
         DWORD e = r ? 0 : GetLastError();
         ENUM_SERVICE_STATUS_PROCESSW *es = (ENUM_SERVICE_STATUS_PROCESSW *)buf;
         DWORD i;
         for (i = 0; i < count; i++) {
             char name[512], esc[512];
             const char *cur;
             int s, j;
             w2utf8(es[i].lpServiceName, name, sizeof name);
             for (j = 0; name[j]; j++) name[j] = (char)tolower((unsigned char)name[j]);
             escape_label(name, esc, sizeof esc);
             cur = service_state_name(es[i].ServiceStatusProcess.dwCurrentState);
             for (s = 0; s < 7; s++)
                 buf_appendf(b, "windows_service_state{name=\"%s\",state=\"%s\"} %d\n",
                             esc, all_states[s], strcmp(all_states[s], cur) == 0 ? 1 : 0);
         }
         if (r) break;
         if (e != ERROR_MORE_DATA) { ok = 0; break; }
     }
     free(buf);
     CloseServiceHandle(scm);
     return ok;
 }
 
 static int collect_disk_health(buf_t *b) {
     int i, any = 0;
     buf_appendf(b,
         "# HELP windows_physical_disk_failure_predicted SMART failure prediction for the physical drive (1 = imminent failure predicted, 0 = healthy)\n"
         "# TYPE windows_physical_disk_failure_predicted gauge\n");
     for (i = 0; i < MAX_DRIVES; i++) {
         int failing = 0;
         if (query_disk_health(i, &failing)) {
             buf_appendf(b, "windows_physical_disk_failure_predicted{disk=\"%d\"} %d\n", i, failing);
             any = 1;
         }
     }
     return any;
 }
 
 /* ------------------------------------------------------------------ */
 /* collector registry (alphabetical; all enabled by default)           */
 /* ------------------------------------------------------------------ */
 
 typedef int (*collector_fn)(buf_t *);
 
 typedef struct {
     const char  *name;
     collector_fn fn;
     int          enabled;
 } collector_t;
 
 static collector_t g_collectors[] = {
     { "cpu",          collect_cpu,          1 },
     { "disk_health",  collect_disk_health,  1 },
     { "logical_disk", collect_logical_disk, 1 },
     { "memory",       collect_memory,       1 },
     { "net",          collect_net,          1 },
     { "service",      collect_services,     1 },
     { "system",       collect_system,       1 },
 };
 #define NCOLLECTORS (sizeof g_collectors / sizeof g_collectors[0])
 
 static int collector_enabled(const char *name) {
     size_t i;
     for (i = 0; i < NCOLLECTORS; i++)
         if (strcmp(g_collectors[i].name, name) == 0)
             return g_collectors[i].enabled;
     return 0;
 }
 
 /* build the full /metrics payload */
 static void build_metrics(buf_t *b) {
     int ok[NCOLLECTORS];
     size_t i;
 
     if (g_query) PdhCollectQueryData(g_query);
 
     for (i = 0; i < NCOLLECTORS; i++)
         if (g_collectors[i].enabled)
             ok[i] = g_collectors[i].fn(b);
 
     buf_appendf(b,
         "# HELP process_start_time_seconds Start time of the process since unix epoch in seconds.\n"
         "# TYPE process_start_time_seconds gauge\n"
         "process_start_time_seconds %.3f\n", g_start_time);
 
     buf_appendf(b, "# HELP windows_exporter_collector_success 1 if the collector succeeded, 0 otherwise\n"
                    "# TYPE windows_exporter_collector_success gauge\n");
     for (i = 0; i < NCOLLECTORS; i++)
         if (g_collectors[i].enabled)
             buf_appendf(b, "windows_exporter_collector_success{collector=\"%s\"} %d\n",
                         g_collectors[i].name, ok[i]);
 }
 
 /* ------------------------------------------------------------------ */
 /* HTTP server (single-threaded, HTTP/1.0, close per request)          */
 /* ------------------------------------------------------------------ */
 
 static volatile LONG g_stop = 0;
 static SOCKET g_listen_sock = INVALID_SOCKET;
 static int g_port = DEFAULT_PORT;
 static unsigned long g_bind_addr = INADDR_ANY; /* network byte order via inet_addr, or INADDR_ANY */
 
 static void send_all(SOCKET s, const char *p, size_t n) {
     while (n > 0) {
         int w = send(s, p, (int)(n > 1 << 20 ? 1 << 20 : n), 0);
         if (w <= 0) return;
         p += w; n -= (size_t)w;
     }
 }
 
 static void handle_client(SOCKET c) {
     char req[2048];
     int n = recv(c, req, sizeof req - 1, 0);
     if (n <= 0) { closesocket(c); return; }
     req[n] = '\0';
 
     {
         size_t plen = strlen(g_metrics_path);
         int is_metrics = strncmp(req, "GET ", 4) == 0 &&
                          strncmp(req + 4, g_metrics_path, plen) == 0 &&
                          (req[4 + plen] == ' ' || req[4 + plen] == '?');
     if (is_metrics) {
         buf_t body;
         char hdr[256];
         buf_init(&body);
         build_metrics(&body);
         _snprintf(hdr, sizeof hdr - 1,
                   "HTTP/1.0 200 OK\r\n"
                   "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
                   "Content-Length: %lu\r\n"
                   "Connection: close\r\n\r\n",
                   (unsigned long)body.len);
         hdr[sizeof hdr - 1] = 0;
         send_all(c, hdr, strlen(hdr));
         send_all(c, body.p, body.len);
         buf_free(&body);
     } else if (strncmp(req, "GET / ", 6) == 0 || strncmp(req, "GET /HTTP", 9) == 0) {
         char page[512];
         _snprintf(page, sizeof page - 1,
             "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
             "<html><body><h1>" EXPORTER_NAME " " EXPORTER_VERSION
             "</h1><p><a href=\"%s\">%s</a></p></body></html>",
             g_metrics_path, g_metrics_path);
         page[sizeof page - 1] = 0;
         send_all(c, page, strlen(page));
     } else {
         static const char nf[] = "HTTP/1.0 404 Not Found\r\nConnection: close\r\n\r\nnot found\n";
         send_all(c, nf, sizeof nf - 1);
     }
     }
     closesocket(c);
 }
 
 static int server_loop(void) {
     WSADATA wsa;
     struct sockaddr_in addr;
 
     if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { log_msg("WSAStartup failed"); return 1; }
 
     g_listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
     if (g_listen_sock == INVALID_SOCKET) { log_msg("socket() failed"); return 1; }
 
     memset(&addr, 0, sizeof addr);
     addr.sin_family = AF_INET;
     addr.sin_addr.s_addr = (g_bind_addr == INADDR_ANY) ? htonl(INADDR_ANY) : g_bind_addr;
     addr.sin_port = htons((u_short)g_port);
     if (bind(g_listen_sock, (struct sockaddr *)&addr, sizeof addr) == SOCKET_ERROR) {
         log_msg("bind(:%d) failed: %d", g_port, WSAGetLastError());
         return 1;
     }
     if (listen(g_listen_sock, 8) == SOCKET_ERROR) { log_msg("listen failed"); return 1; }
 
     log_msg("%s %s listening on :%d", EXPORTER_NAME, EXPORTER_VERSION, g_port);
     while (!g_stop) {
         SOCKET c = accept(g_listen_sock, NULL, NULL);
         if (c == INVALID_SOCKET) {
             if (g_stop) break;
             continue;
         }
         handle_client(c);
     }
     closesocket(g_listen_sock);
     WSACleanup();
     return 0;
 }
 
 /* ------------------------------------------------------------------ */
 /* Windows service (SCM) integration                                   */
 /* ------------------------------------------------------------------ */
 
 static SERVICE_STATUS_HANDLE g_svc_status_handle;
 
 static void set_svc_state(DWORD state, DWORD accepts) {
     SERVICE_STATUS st;
     memset(&st, 0, sizeof st);
     st.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
     st.dwCurrentState = state;
     st.dwControlsAccepted = accepts;
     SetServiceStatus(g_svc_status_handle, &st);
 }
 
 static void WINAPI svc_ctrl_handler(DWORD ctrl) {
     switch (ctrl) {
         case SERVICE_CONTROL_STOP:
         case SERVICE_CONTROL_SHUTDOWN:
             set_svc_state(SERVICE_STOP_PENDING, 0);
             InterlockedExchange(&g_stop, 1);
             if (g_listen_sock != INVALID_SOCKET) closesocket(g_listen_sock);
             break;
         default:
             break;
     }
 }
 
 static void WINAPI svc_main(DWORD argc, LPWSTR *argv) {
     (void)argc; (void)argv;
     g_svc_status_handle = RegisterServiceCtrlHandlerW(L"" EXPORTER_NAME, svc_ctrl_handler);
     if (!g_svc_status_handle) return;
     set_svc_state(SERVICE_RUNNING, SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
     server_loop();
     set_svc_state(SERVICE_STOPPED, 0);
 }
 
 /* ------------------------------------------------------------------ */
 /* main                                                                */
 /* ------------------------------------------------------------------ */
 
 static void usage(void) {
     size_t i;
     printf("usage: %s [flags]\n"
            "\n"
            "  --web.listen-address ADDR   address for the metrics endpoint, e.g. \":9182\",\n"
            "                              \"9182\" or \"192.168.1.10:9182\" (default \":%d\")\n"
            "  --telemetry.addr ADDR       alias of --web.listen-address (windows_exporter compat)\n"
            "  --telemetry.path PATH       URL path for metrics (default \"/metrics\")\n"
            "  --collectors.enabled LIST   comma-separated collectors to enable; the token\n"
            "                              [defaults] expands to all of them (default: all)\n"
            "  --collectors.print          print available collectors and exit\n"
            "  --version                   print version and exit\n"
            "  --help                      this text\n"
            "\nAll flags also accept the --flag=value form.\n"
            "\navailable collectors:", EXPORTER_NAME, DEFAULT_PORT);
     for (i = 0; i < NCOLLECTORS; i++) printf(" %s", g_collectors[i].name);
     printf("\n");
 }
 
 /* accepts "--flag value" and "--flag=value"; returns the value or exits */
 static const char *flag_value(int argc, char **argv, int *i, const char *eq) {
     if (eq) return eq + 1;
     if (*i + 1 < argc) return argv[++*i];
     log_msg("error: flag %s requires a value", argv[*i]);
     exit(2);
 }
 
 /* matches "--name" or "--name=..."; sets *eq to the '=' if present */
 static int flag_is(const char *arg, const char *name, const char **eq) {
     size_t n = strlen(name);
     if (strncmp(arg, name, n) != 0) return 0;
     if (arg[n] == '\0') { *eq = NULL; return 1; }
     if (arg[n] == '=')  { *eq = arg + n; return 1; }
     return 0;
 }
 
 static void set_listen_address(const char *v) {
     const char *colon = strrchr(v, ':');
     char host[64];
     const char *portstr;
 
     if (colon) {
         size_t hl = (size_t)(colon - v);
         if (hl >= sizeof host) hl = sizeof host - 1;
         memcpy(host, v, hl);
         host[hl] = '\0';
         portstr = colon + 1;
     } else {
         host[0] = '\0';
         portstr = v;       /* bare port number */
     }
 
     g_port = atoi(portstr);
     if (g_port <= 0 || g_port > 65535) {
         log_msg("error: invalid port in listen address \"%s\"", v);
         exit(2);
     }
     if (host[0]) {
         unsigned long a = inet_addr(host);
         if (a == INADDR_NONE) {
             log_msg("error: invalid bind IP \"%s\" (use a numeric IPv4 address or omit the host)", host);
             exit(2);
         }
         g_bind_addr = a;
     }
 }
 
 static void set_enabled_collectors(const char *list) {
     char copy[512];
     char *tok, *saveptr = NULL;
     size_t i;
 
     for (i = 0; i < NCOLLECTORS; i++) g_collectors[i].enabled = 0;
 
     strncpy(copy, list, sizeof copy - 1);
     copy[sizeof copy - 1] = '\0';
 
     for (tok = strtok_r(copy, ",", &saveptr); tok; tok = strtok_r(NULL, ",", &saveptr)) {
         int found = 0;
         while (*tok == ' ') tok++;
         if (strcmp(tok, "[defaults]") == 0) {
             for (i = 0; i < NCOLLECTORS; i++) g_collectors[i].enabled = 1;
             continue;
         }
         for (i = 0; i < NCOLLECTORS; i++) {
             if (strcmp(tok, g_collectors[i].name) == 0) {
                 g_collectors[i].enabled = 1;
                 found = 1;
                 break;
             }
         }
         if (!found) {
             log_msg("error: unknown collector \"%s\" in --collectors.enabled", tok);
             log_msg("run with --collectors.print to list the available collectors");
             exit(2);
         }
     }
 }
 
 static void parse_args(int argc, char **argv) {
     int i;
     const char *eq;
     for (i = 1; i < argc; i++) {
         const char *a = argv[i];
         if (flag_is(a, "--web.listen-address", &eq) || flag_is(a, "--telemetry.addr", &eq)) {
             set_listen_address(flag_value(argc, argv, &i, eq));
         } else if (flag_is(a, "--telemetry.path", &eq)) {
             const char *v = flag_value(argc, argv, &i, eq);
             if (v[0] != '/') { log_msg("error: --telemetry.path must start with '/'"); exit(2); }
             strncpy(g_metrics_path, v, sizeof g_metrics_path - 1);
             g_metrics_path[sizeof g_metrics_path - 1] = '\0';
         } else if (flag_is(a, "--collectors.enabled", &eq)) {
             set_enabled_collectors(flag_value(argc, argv, &i, eq));
         } else if (strcmp(a, "--collectors.print") == 0) {
             size_t j;
             for (j = 0; j < NCOLLECTORS; j++) printf("%s\n", g_collectors[j].name);
             exit(0);
         } else if (strcmp(a, "--version") == 0 || strcmp(a, "-version") == 0) {
             printf("%s %s\n", EXPORTER_NAME, EXPORTER_VERSION);
             exit(0);
         } else if (strcmp(a, "--help") == 0 || strcmp(a, "-help") == 0 || strcmp(a, "-h") == 0) {
             usage();
             exit(0);
         /* deprecated aliases kept for compatibility with v0.2.0 */
         } else if (strcmp(a, "-listen") == 0 && i + 1 < argc) {
             set_listen_address(argv[++i]);
         } else if (strcmp(a, "-no-services") == 0) {
             size_t j;
             for (j = 0; j < NCOLLECTORS; j++)
                 if (strcmp(g_collectors[j].name, "service") == 0)
                     g_collectors[j].enabled = 0;
         } else {
             log_msg("error: unknown flag \"%s\" (see --help)", a);
             exit(2);
         }
     }
 }
 
 int main(int argc, char **argv) {
     SERVICE_TABLE_ENTRYW table[2];
     int want_cpu, want_disk, want_net, want_sys;
 
     parse_args(argc, argv);
     g_start_time = filetime_unix_now();
 
     want_cpu  = collector_enabled("cpu");
     want_disk = collector_enabled("logical_disk");
     want_net  = collector_enabled("net");
     want_sys  = collector_enabled("system");
     if (want_cpu || want_disk || want_net || want_sys) {
         if (!pdh_init(want_cpu, want_disk, want_net, want_sys)) {
             log_msg("warning: PDH initialisation failed; cpu, disk IO, network and system metrics will be absent");
         }
     }
 
     /* Try SCM first; error 1063 means we were started from a console. */
     table[0].lpServiceName = (LPWSTR)L"" EXPORTER_NAME;
     table[0].lpServiceProc = svc_main;
     table[1].lpServiceName = NULL;
     table[1].lpServiceProc = NULL;
     if (StartServiceCtrlDispatcherW(table)) return 0;
     if (GetLastError() != ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
         log_msg("StartServiceCtrlDispatcherW failed: %lu", GetLastError());
     }
 
     return server_loop();
 }