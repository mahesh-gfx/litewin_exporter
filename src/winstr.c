#include "winstr.h"

#include <windows.h>

void w2utf8(const wchar_t *w, char *out, int outsz)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, outsz, NULL, NULL);
    if (n <= 0 && outsz > 0)
        out[0] = '\0';
    out[outsz - 1] = '\0';
}
