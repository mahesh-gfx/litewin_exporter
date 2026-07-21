#include "collectors/service.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *ALL_STATES[7] = {
    "continue pending", "pause pending", "paused", "running",
    "start pending", "stop pending", "stopped"
};

const char *service_state_name(unsigned state)
{
    switch (state) {
    case 1: return "stopped";           /* SERVICE_STOPPED */
    case 2: return "start pending";     /* SERVICE_START_PENDING */
    case 3: return "stop pending";      /* SERVICE_STOP_PENDING */
    case 4: return "running";           /* SERVICE_RUNNING */
    case 5: return "continue pending";  /* SERVICE_CONTINUE_PENDING */
    case 6: return "pause pending";     /* SERVICE_PAUSE_PENDING */
    case 7: return "paused";            /* SERVICE_PAUSED */
    default: return "unknown";
    }
}

void service_emit_states(buf_t *b, const char *name, const char *current)
{
    char low[512], esc[512], labels[600];
    int i, j;

    for (j = 0; name[j] && j < (int)sizeof low - 1; j++)
        low[j] = (char)tolower((unsigned char)name[j]);
    low[j] = '\0';
    tf_escape_label(low, esc, sizeof esc);

    for (i = 0; i < 7; i++) {
        snprintf(labels, sizeof labels, "name=\"%s\",state=\"%s\"", esc, ALL_STATES[i]);
        tf_sample(b, "windows_service_state", labels,
                  strcmp(ALL_STATES[i], current) == 0 ? 1 : 0);
    }
}

#ifdef _WIN32
#include <windows.h>
#include "winstr.h"
#include <stdlib.h>

int collect_service(buf_t *b)
{
    SC_HANDLE scm;
    BYTE *buf;
    DWORD bufsz = 256 * 1024, needed = 0, count = 0, resume = 0;
    int ok = 1;

    scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm)
        return 0;
    buf = (BYTE *)malloc(bufsz);
    if (!buf) {
        CloseServiceHandle(scm);
        return 0;
    }

    tf_family(b, "windows_service_state", "gauge",
              "Service state, one-hot over the 7 states (the set value is 1).");
    for (;;) {
        BOOL r = EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                                       SERVICE_STATE_ALL, buf, bufsz,
                                       &needed, &count, &resume, NULL);
        DWORD e = r ? 0 : GetLastError();
        ENUM_SERVICE_STATUS_PROCESSW *es = (ENUM_SERVICE_STATUS_PROCESSW *)buf;
        DWORD i;
        for (i = 0; i < count; i++) {
            char name[512];
            w2utf8(es[i].lpServiceName, name, sizeof name);
            service_emit_states(b, name,
                                service_state_name(es[i].ServiceStatusProcess.dwCurrentState));
        }
        if (r)
            break;
        if (e != ERROR_MORE_DATA) {
            ok = 0;
            break;
        }
    }
    free(buf);
    CloseServiceHandle(scm);
    return ok;
}
#endif
