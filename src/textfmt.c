#include "textfmt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

void buf_init_cap(buf_t *b, size_t cap) {
    if (cap < 16) cap = 16;
    b->cap = cap;
    b->len = 0;
    b->p = (char *)malloc(b->cap);
    if (b->p) b->p[0] = '\0';
}

void buf_init(buf_t *b) { buf_init_cap(b, 64 * 1024); }

void buf_free(buf_t *b) { free(b->p); b->p = NULL; b->len = b->cap = 0; }

void buf_appendf(buf_t *b, const char *fmt, ...) {
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

void tf_escape_label(const char *in, char *out, int outsz) {
    int j = 0, i;
    for (i = 0; in[i] && j < outsz - 3; i++) {
        char c = in[i];
        if (c == '\\' || c == '"') { out[j++] = '\\'; out[j++] = c; }
        else if (c == '\n')        { out[j++] = '\\'; out[j++] = 'n'; }
        else                        out[j++] = c;
    }
    out[j] = '\0';
}

void tf_family(buf_t *b, const char *name, const char *type, const char *help) {
    buf_appendf(b, "# HELP %s %s\n# TYPE %s %s\n", name, help, name, type);
}

void tf_sample(buf_t *b, const char *name, const char *labels, double value) {
    if (labels && labels[0])
        buf_appendf(b, "%s{%s} %.15g\n", name, labels, value);
    else
        buf_appendf(b, "%s %.15g\n", name, value);
}