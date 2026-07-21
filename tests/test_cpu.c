/* Unit tests for the pure CPU tick maths + sample format (M6, tier 1). */
#include <stdio.h>
#include <string.h>

#include "unity.h"
#include "collectors/cpu.h"

static buf_t b;

void setUp(void) { buf_init(&b); }
void tearDown(void) { buf_free(&b); }

/* 1e7 * 100 ns == 1 s; check via the exposition-formatted value. */
static void test_ticks_to_seconds(void)
{
    char s[32];
    snprintf(s, sizeof s, "%.15g", cpu_ticks_to_seconds(0));         TEST_ASSERT_EQUAL_STRING("0", s);
    snprintf(s, sizeof s, "%.15g", cpu_ticks_to_seconds(10000000LL)); TEST_ASSERT_EQUAL_STRING("1", s);
    snprintf(s, sizeof s, "%.15g", cpu_ticks_to_seconds(5000000LL));  TEST_ASSERT_EQUAL_STRING("0.5", s);
    snprintf(s, sizeof s, "%.15g", cpu_ticks_to_seconds(12345LL));    TEST_ASSERT_EQUAL_STRING("0.0012345", s);
}

static void test_sample_line_format(void)
{
    cpu_time_sample(&b, "3", "privileged", 10000000LL);
    TEST_ASSERT_NOT_NULL(
        strstr(b.p, "windows_cpu_time_total{core=\"0,3\",mode=\"privileged\"} 1\n"));
}

static void test_sample_zero_and_fraction(void)
{
    cpu_time_sample(&b, "0", "idle", 0);
    cpu_time_sample(&b, "1", "dpc", 5000000LL);
    TEST_ASSERT_NOT_NULL(strstr(b.p, "{core=\"0,0\",mode=\"idle\"} 0\n"));
    TEST_ASSERT_NOT_NULL(strstr(b.p, "{core=\"0,1\",mode=\"dpc\"} 0.5\n"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ticks_to_seconds);
    RUN_TEST(test_sample_line_format);
    RUN_TEST(test_sample_zero_and_fraction);
    return UNITY_END();
}
