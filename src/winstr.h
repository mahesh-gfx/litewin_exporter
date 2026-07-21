#ifndef LITEWIN_WINSTR_H
#define LITEWIN_WINSTR_H

#include <wchar.h>

/* UTF-16 -> UTF-8, always NUL-terminated. Windows only. */
void w2utf8(const wchar_t *w, char *out, int outsz);

#endif
