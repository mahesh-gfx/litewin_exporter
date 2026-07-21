/* Unit test for the pure bits/sec -> bytes/sec conversion (M9, tier 1). */
#include <stdio.h>
#include <string.h>

#include "unity.h"
#include "collectors/net.h"

void setUp(void) {}
void tearDown(void) {}

static void test_bits_to_bytes(void)
{
    char s[32];
    snprintf(s, sizeof s, "%.15g", net_bw_bits_to_bytes(0));           TEST_ASSERT_EQUAL_STRING("0", s);
    snprintf(s, sizeof s, "%.15g", net_bw_bits_to_bytes(8));           TEST_ASSERT_EQUAL_STRING("1", s);
    snprintf(s, sizeof s, "%.15g", net_bw_bits_to_bytes(1000000000LL)); TEST_ASSERT_EQUAL_STRING("125000000", s);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_bits_to_bytes);
    return UNITY_END();
}
