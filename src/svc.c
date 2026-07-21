#include "svc.h"
#include "http.h"
#include "log.h"
#include "version.h"

#include <windows.h>
#include <string.h>

/* Stashed by svc_start() so the SCM callbacks (fixed signatures) can reach them. */
static http_handler_fn g_handler;
static unsigned long g_bind;
static int g_port;
static SERVICE_STATUS_HANDLE g_ssh;

static void set_state(DWORD state, DWORD accepts)
{
    SERVICE_STATUS st;
    memset(&st, 0, sizeof st);
    st.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    st.dwCurrentState = state;
    st.dwControlsAccepted = accepts;
    SetServiceStatus(g_ssh, &st);
}

static void WINAPI ctrl_handler(DWORD ctrl)
{
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        set_state(SERVICE_STOP_PENDING, 0);
        http_stop(); /* closes the listen socket -> server_loop returns */
        break;
    default:
        break;
    }
}

static void WINAPI svc_main(DWORD argc, LPWSTR *argv)
{
    (void)argc;
    (void)argv;
    g_ssh = RegisterServiceCtrlHandlerW(L"" LITEWIN_NAME, ctrl_handler);
    if (!g_ssh)
        return;
    set_state(SERVICE_RUNNING, SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN);
    http_serve(g_bind, g_port, g_handler);
    set_state(SERVICE_STOPPED, 0);
}

int svc_start(unsigned long bind_addr, int port, http_handler_fn handler)
{
    SERVICE_TABLE_ENTRYW table[2];

    g_handler = handler;
    g_bind = bind_addr;
    g_port = port;

    table[0].lpServiceName = (LPWSTR)L"" LITEWIN_NAME;
    table[0].lpServiceProc = svc_main;
    table[1].lpServiceName = NULL;
    table[1].lpServiceProc = NULL;

    if (StartServiceCtrlDispatcherW(table))
        return 0; /* ran as a service; dispatcher returned after stop */
    if (GetLastError() != ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
        log_msg("StartServiceCtrlDispatcherW failed: %lu", (unsigned long)GetLastError());
        return 1;
    }
    return http_serve(g_bind, g_port, g_handler); /* console mode */
}
