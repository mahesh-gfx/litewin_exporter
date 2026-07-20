/* Unit tests for the pure memory format helper (M5, tier 1). */
#include <string.h>

#include "unity.h"
#include "collectors/memory.h"

static buf_t b;

void setUp(void) { buf_init(&b); }
void tearDown(void) { buf_free(&b); }

static void test_emits_all_three_families(void)
{
    memory_format(&b, 17179869184ULL, 8589934592ULL, 34359738368ULL);

    /* HELP/TYPE present for each family */
    TEST_ASSERT_NOT_NULL(strstr(b.p, "# TYPE windows_memory_physical_total_bytes gauge\n"));
    TEST_ASSERT_NOT_NULL(strstr(b.p, "# TYPE windows_memory_physical_free_bytes gauge\n"));
    TEST_ASSERT_NOT_NULL(strstr(b.p, "# TYPE windows_memory_commit_limit gauge\n"));
}

static void test_values_render_exactly(void)
{
    /* 16 GiB total, 8 GiB free, 32 GiB commit limit — exact in a double */
    memory_format(&b, 17179869184ULL, 8589934592ULL, 34359738368ULL);
    TEST_ASSERT_NOT_NULL(strstr(b.p, "\nwindows_memory_physical_total_bytes 17179869184\n"));
    TEST_ASSERT_NOT_NULL(strstr(b.p, "\nwindows_memory_physical_free_bytes 8589934592\n"));
    TEST_ASSERT_NOT_NULL(strstr(b.p, "\nwindows_memory_commit_limit 34359738368\n"));
}

static void test_zero_values(void)
{
    memory_format(&b, 0, 0, 0);
    TEST_ASSERT_NOT_NULL(strstr(b.p, "\nwindows_memory_physical_total_bytes 0\n"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_emits_all_three_families);
    RUN_TEST(test_values_render_exactly);
    RUN_TEST(test_zero_values);
    return UNITY_END();
}
