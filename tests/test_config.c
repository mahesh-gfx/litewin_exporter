/* Unit tests for src/config.c — written first (M2, tier 1). */
#include "unity.h"
#include "config.h"

static config_t cfg;

void setUp(void) {}
void tearDown(void) {}

/* Build argv (argv[0] = "exe"), parse into cfg, leave result in `r`. */
#define PARSE(...)                                                       \
    char *argv[] = {"exe", __VA_ARGS__};                                 \
    config_result_t r = config_parse(&cfg, (int)(sizeof argv / sizeof *argv), argv)

static void test_defaults(void)
{
    char *argv[] = {"exe"};
    config_result_t r = config_parse(&cfg, 1, argv);
    TEST_ASSERT_EQUAL(CONFIG_OK, r);
    TEST_ASSERT_EQUAL_INT(9182, cfg.port);
    TEST_ASSERT_EQUAL_STRING("/metrics", cfg.metrics_path);
    TEST_ASSERT_EACH_EQUAL_UINT8(0, cfg.bind_ip, 4);
}

static void test_listen_colon_port(void)
{
    PARSE("--web.listen-address", ":9100");
    TEST_ASSERT_EQUAL(CONFIG_OK, r);
    TEST_ASSERT_EQUAL_INT(9100, cfg.port);
    TEST_ASSERT_EACH_EQUAL_UINT8(0, cfg.bind_ip, 4);
}

static void test_listen_bare_port(void)
{
    PARSE("--web.listen-address", "9100");
    TEST_ASSERT_EQUAL(CONFIG_OK, r);
    TEST_ASSERT_EQUAL_INT(9100, cfg.port);
}

static void test_listen_ip_and_port(void)
{
    unsigned char want[4] = {192, 168, 1, 10};
    PARSE("--web.listen-address", "192.168.1.10:9100");
    TEST_ASSERT_EQUAL(CONFIG_OK, r);
    TEST_ASSERT_EQUAL_INT(9100, cfg.port);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, cfg.bind_ip, 4);
}

static void test_listen_equals_form(void)
{
    PARSE("--web.listen-address=:9101");
    TEST_ASSERT_EQUAL(CONFIG_OK, r);
    TEST_ASSERT_EQUAL_INT(9101, cfg.port);
}

static void test_telemetry_addr_alias(void)
{
    PARSE("--telemetry.addr", ":9102");
    TEST_ASSERT_EQUAL(CONFIG_OK, r);
    TEST_ASSERT_EQUAL_INT(9102, cfg.port);
}

static void test_telemetry_path(void)
{
    PARSE("--telemetry.path", "/probe");
    TEST_ASSERT_EQUAL(CONFIG_OK, r);
    TEST_ASSERT_EQUAL_STRING("/probe", cfg.metrics_path);
}

static void test_telemetry_path_equals_form(void)
{
    PARSE("--telemetry.path=/probe2");
    TEST_ASSERT_EQUAL(CONFIG_OK, r);
    TEST_ASSERT_EQUAL_STRING("/probe2", cfg.metrics_path);
}

static void test_path_must_start_with_slash(void)
{
    PARSE("--telemetry.path", "probe");
    TEST_ASSERT_EQUAL(CONFIG_ERR, r);
}

static void test_port_zero_rejected(void)
{
    PARSE("--web.listen-address", ":0");
    TEST_ASSERT_EQUAL(CONFIG_ERR, r);
}

static void test_port_too_big_rejected(void)
{
    PARSE("--web.listen-address", ":70000");
    TEST_ASSERT_EQUAL(CONFIG_ERR, r);
}

static void test_port_not_numeric_rejected(void)
{
    PARSE("--web.listen-address", ":abc");
    TEST_ASSERT_EQUAL(CONFIG_ERR, r);
}

static void test_port_trailing_junk_rejected(void)
{
    PARSE("--web.listen-address", "9182x");
    TEST_ASSERT_EQUAL(CONFIG_ERR, r);
}

static void test_ip_octet_out_of_range_rejected(void)
{
    PARSE("--web.listen-address", "999.1.1.1:9100");
    TEST_ASSERT_EQUAL(CONFIG_ERR, r);
}

static void test_ip_not_numeric_rejected(void)
{
    PARSE("--web.listen-address", "banana:9100");
    TEST_ASSERT_EQUAL(CONFIG_ERR, r);
}

static void test_unknown_flag_rejected(void)
{
    PARSE("--bogus");
    TEST_ASSERT_EQUAL(CONFIG_ERR, r);
}

static void test_missing_value_rejected(void)
{
    PARSE("--web.listen-address");
    TEST_ASSERT_EQUAL(CONFIG_ERR, r);
}

static void test_version_exits_zero(void)
{
    PARSE("--version");
    TEST_ASSERT_EQUAL(CONFIG_EXIT0, r);
}

static void test_help_exits_zero(void)
{
    PARSE("--help");
    TEST_ASSERT_EQUAL(CONFIG_EXIT0, r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_listen_colon_port);
    RUN_TEST(test_listen_bare_port);
    RUN_TEST(test_listen_ip_and_port);
    RUN_TEST(test_listen_equals_form);
    RUN_TEST(test_telemetry_addr_alias);
    RUN_TEST(test_telemetry_path);
    RUN_TEST(test_telemetry_path_equals_form);
    RUN_TEST(test_path_must_start_with_slash);
    RUN_TEST(test_port_zero_rejected);
    RUN_TEST(test_port_too_big_rejected);
    RUN_TEST(test_port_not_numeric_rejected);
    RUN_TEST(test_port_trailing_junk_rejected);
    RUN_TEST(test_ip_octet_out_of_range_rejected);
    RUN_TEST(test_ip_not_numeric_rejected);
    RUN_TEST(test_unknown_flag_rejected);
    RUN_TEST(test_missing_value_rejected);
    RUN_TEST(test_version_exits_zero);
    RUN_TEST(test_help_exits_zero);
    return UNITY_END();
}
