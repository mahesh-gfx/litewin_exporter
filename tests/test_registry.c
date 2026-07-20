/* Unit tests for the pure enable-set resolution. */
#include "unity.h"
#include "registry.h"

static collector_t cols[3];
static char unknown[64];

void setUp(void)
{
    cols[0].name = "cpu";    cols[0].init = NULL; cols[0].collect = NULL; cols[0].enabled = -1;
    cols[1].name = "memory"; cols[1].init = NULL; cols[1].collect = NULL; cols[1].enabled = -1;
    cols[2].name = "net";    cols[2].init = NULL; cols[2].collect = NULL; cols[2].enabled = -1;
    unknown[0] = '\0';
}
void tearDown(void) {}

#define ENABLED(a, b, c)                    \
    TEST_ASSERT_EQUAL_INT((a), cols[0].enabled); \
    TEST_ASSERT_EQUAL_INT((b), cols[1].enabled); \
    TEST_ASSERT_EQUAL_INT((c), cols[2].enabled)

static void test_null_enables_all(void)
{
    TEST_ASSERT_EQUAL_INT(1, collectors_resolve(cols, 3, NULL, unknown, sizeof unknown));
    ENABLED(1, 1, 1);
}

static void test_empty_enables_all(void)
{
    TEST_ASSERT_EQUAL_INT(1, collectors_resolve(cols, 3, "", unknown, sizeof unknown));
    ENABLED(1, 1, 1);
}

static void test_defaults_token_enables_all(void)
{
    TEST_ASSERT_EQUAL_INT(1, collectors_resolve(cols, 3, "[defaults]", unknown, sizeof unknown));
    ENABLED(1, 1, 1);
}

static void test_subset(void)
{
    TEST_ASSERT_EQUAL_INT(1, collectors_resolve(cols, 3, "cpu,net", unknown, sizeof unknown));
    ENABLED(1, 0, 1);
}

static void test_leading_spaces_tolerated(void)
{
    TEST_ASSERT_EQUAL_INT(1, collectors_resolve(cols, 3, "cpu, memory", unknown, sizeof unknown));
    ENABLED(1, 1, 0);
}

static void test_unknown_reports_name(void)
{
    TEST_ASSERT_EQUAL_INT(0, collectors_resolve(cols, 3, "cpu,bogus", unknown, sizeof unknown));
    TEST_ASSERT_EQUAL_STRING("bogus", unknown);
}

static void test_defaults_token_within_list(void)
{
    TEST_ASSERT_EQUAL_INT(1, collectors_resolve(cols, 3, "net,[defaults]", unknown, sizeof unknown));
    ENABLED(1, 1, 1);
}

static void test_empty_registry_ok(void)
{
    /* zero collectors (the M4 state): any explicit name is unknown */
    TEST_ASSERT_EQUAL_INT(1, collectors_resolve(NULL, 0, NULL, unknown, sizeof unknown));
    TEST_ASSERT_EQUAL_INT(0, collectors_resolve(NULL, 0, "memory", unknown, sizeof unknown));
    TEST_ASSERT_EQUAL_STRING("memory", unknown);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_null_enables_all);
    RUN_TEST(test_empty_enables_all);
    RUN_TEST(test_defaults_token_enables_all);
    RUN_TEST(test_subset);
    RUN_TEST(test_leading_spaces_tolerated);
    RUN_TEST(test_unknown_reports_name);
    RUN_TEST(test_defaults_token_within_list);
    RUN_TEST(test_empty_registry_ok);
    return UNITY_END();
}
