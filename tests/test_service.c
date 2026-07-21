/* Unit tests for the pure service state logic (M11, tier 1). */
#include <string.h>

#include "unity.h"
#include "collectors/service.h"

static buf_t b;

void setUp(void) { buf_init(&b); }
void tearDown(void) { buf_free(&b); }

static void test_state_names(void)
{
    TEST_ASSERT_EQUAL_STRING("stopped", service_state_name(1));
    TEST_ASSERT_EQUAL_STRING("running", service_state_name(4));
    TEST_ASSERT_EQUAL_STRING("paused", service_state_name(7));
    TEST_ASSERT_EQUAL_STRING("unknown", service_state_name(99));
}

/* Count " 1\n" vs " 0\n" endings to prove exactly one state is hot. */
static void test_one_hot_sum_is_one(void)
{
    const char *p;
    int ones = 0, zeros = 0;
    service_emit_states(&b, "Spooler", "running");
    for (p = b.p; (p = strstr(p, "windows_service_state{")) != NULL; p++) {
        const char *nl = strchr(p, '\n');
        if (nl && nl[-1] == '1' && nl[-2] == ' ') ones++;
        else if (nl && nl[-1] == '0' && nl[-2] == ' ') zeros++;
    }
    TEST_ASSERT_EQUAL_INT(1, ones);
    TEST_ASSERT_EQUAL_INT(6, zeros);
}

static void test_lowercased_and_running_is_hot(void)
{
    service_emit_states(&b, "Spooler", "running");
    TEST_ASSERT_NOT_NULL(strstr(b.p, "name=\"spooler\",state=\"running\"} 1\n"));
    TEST_ASSERT_NOT_NULL(strstr(b.p, "name=\"spooler\",state=\"stopped\"} 0\n"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_state_names);
    RUN_TEST(test_one_hot_sum_is_one);
    RUN_TEST(test_lowercased_and_running_is_hot);
    return UNITY_END();
}
