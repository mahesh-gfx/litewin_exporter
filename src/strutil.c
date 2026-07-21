#include "strutil.h"

#include <ctype.h>
#include <string.h>

void lw_sanitize_nic(const char *in, char *out, int outsz)
{
    int i;
    for (i = 0; in[i] && i < outsz - 1; i++) {
        char c = in[i];
        out[i] = isalnum((unsigned char)c) ? c : '_';
    }
    out[i] = '\0';
}

void lw_instance_volume(const char *inst, char *out, int outsz)
{
    const char *sp = strrchr(inst, ' ');
    const char *v = sp ? sp + 1 : inst;
    int i = 0;
    while (v[i] && i < outsz - 1) {
        out[i] = v[i];
        i++;
    }
    out[i] = '\0';
}
