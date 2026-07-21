#include "unity.h"
#include "textfmt.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* --- tf_escape_label ---------------------------------------------------- */

static void escape(const char *in, char *out) { tf_escape_label(in, out, 256); }

void test_escape_passes_plain_text_through(void) {
    char out[256];
    escape("Local Area Connection", out);
    TEST_ASSERT_EQUAL_STRING("Local Area Connection", out);
}

void test_escape_backslash(void) {
    char out[256];
    escape("a\\b", out);
    TEST_ASSERT_EQUAL_STRING("a\\\\b", out);
}

void test_escape_double_quote(void) {
    char out[256];
    escape("say \"hi\"", out);
    TEST_ASSERT_EQUAL_STRING("say \\\"hi\\\"", out);
}

void test_escape_newline(void) {
    char out[256];
    escape("line1\nline2", out);
    TEST_ASSERT_EQUAL_STRING("line1\\nline2", out);
}

void test_escape_never_overflows_small_buffer(void) {
    char out[8];
    tf_escape_label("\"\"\"\"\"\"\"\"\"\"", out, (int)sizeof out); /* all need escaping */
    TEST_ASSERT_TRUE(strlen(out) < sizeof out);                   /* stayed in bounds */
}

/* --- buf growth --------------------------------------------------------- */

void test_buf_grows_past_initial_capacity(void) {
    buf_t b;
    int i;
    buf_init_cap(&b, 16);              /* force many reallocations */
    for (i = 0; i < 1000; i++) buf_appendf(&b, "line %d\n", i);
    /* content is intact and length matches what we wrote */
    TEST_ASSERT_NOT_NULL(b.p);
    TEST_ASSERT_TRUE(b.len > 16);
    TEST_ASSERT_EQUAL_INT(0, strncmp(b.p, "line 0\n", 7));
    TEST_ASSERT_NOT_NULL(strstr(b.p, "line 999\n"));
    buf_free(&b);
}

void test_buf_appendf_formats_like_printf(void) {
    buf_t b;
    buf_init_cap(&b, 64);
    buf_appendf(&b, "%s{k=\"%d\"} %.1f\n", "metric", 7, 3.5);
    TEST_ASSERT_EQUAL_STRING("metric{k=\"7\"} 3.5\n", b.p);
    buf_free(&b);
}

/* --- tf_family / tf_sample ---------------------------------------------- */

void test_tf_family_emits_help_and_type(void) {
    buf_t b;
    buf_init_cap(&b, 64);
    tf_family(&b, "foo_total", "counter", "Total foos.");
    TEST_ASSERT_EQUAL_STRING(
        "# HELP foo_total Total foos.\n# TYPE foo_total counter\n", b.p);
    buf_free(&b);
}

void test_tf_sample_with_and_without_labels(void) {
    buf_t b;
    buf_init_cap(&b, 64);
    tf_sample(&b, "foo", "core=\"0\"", 1.5);
    tf_sample(&b, "bar", NULL, 1752854123.0); /* unix timestamps stay exact */
    TEST_ASSERT_EQUAL_STRING("foo{core=\"0\"} 1.5\nbar 1752854123\n", b.p);
    buf_free(&b);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_escape_passes_plain_text_through);
    RUN_TEST(test_escape_backslash);
    RUN_TEST(test_escape_double_quote);
    RUN_TEST(test_escape_newline);
    RUN_TEST(test_escape_never_overflows_small_buffer);
    RUN_TEST(test_buf_grows_past_initial_capacity);
    RUN_TEST(test_buf_appendf_formats_like_printf);
    RUN_TEST(test_tf_family_emits_help_and_type);
    RUN_TEST(test_tf_sample_with_and_without_labels);
    return UNITY_END();
}