/* Unit tests for the pure label helpers (M7 volume, M9 nic; tier 1). */
#include <string.h>

#include "unity.h"
#include "strutil.h"

void setUp(void) {}
void tearDown(void) {}

/* --- lw_sanitize_nic --- */

static void test_nic_plain_unchanged(void)
{
    char out[128];
    lw_sanitize_nic("Ethernet0", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("Ethernet0", out);
}

static void test_nic_spaces_and_punct_to_underscore(void)
{
    char out[128];
    lw_sanitize_nic("Local Area Connection* 2", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("Local_Area_Connection__2", out);
}

static void test_nic_never_overflows(void)
{
    char out[4];
    lw_sanitize_nic("abcdefgh", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("abc", out); /* 3 chars + NUL */
}

/* --- lw_instance_volume --- */

static void test_instance_plain_volume(void)
{
    char out[64];
    lw_instance_volume("C:", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("C:", out);
}

static void test_instance_numbered_takes_last_token(void)
{
    char out[64];
    lw_instance_volume("1 C:", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("C:", out);
}

static void test_instance_multi_token(void)
{
    char out[64];
    lw_instance_volume("0 C: D:", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("D:", out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nic_plain_unchanged);
    RUN_TEST(test_nic_spaces_and_punct_to_underscore);
    RUN_TEST(test_nic_never_overflows);
    RUN_TEST(test_instance_plain_volume);
    RUN_TEST(test_instance_numbered_takes_last_token);
    RUN_TEST(test_instance_multi_token);
    return UNITY_END();
}
