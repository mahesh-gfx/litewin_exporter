#ifndef LITEWIN_TEXTFMT_H
#define LITEWIN_TEXTFMT_H

#include <stddef.h>

/* Growable text buffer for the /metrics exposition. no windows.h. */
typedef struct {
    char *p; /* malloc'd, NUL-terminated; NULL after alloc failure */
    size_t len;
    size_t cap;
} buf_t;

void buf_init(buf_t *b);
void buf_init_cap(buf_t *b, size_t cap);
void buf_free(buf_t *b);
void buf_appendf(buf_t *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Prometheus label-value escaping: backslash, double quote, newline. */
void tf_escape_label(const char *in, char *out, int outsz);

/* "# HELP name help" + "# TYPE name type" lines. */
void tf_family(buf_t *b, const char *name, const char *type, const char *help);

/* One sample line; labels is the pre-formatted k="v" list or NULL for none. */
void tf_sample(buf_t *b, const char *name, const char *labels, double value);

#endif
