#include "../src/config.h"
#include "../src/util.h"

#include <stdlib.h>
#include <string.h>
#include <unity.h>

void setUp(void)
{
    // set stuff up here
}

void tearDown(void)
{
    // clean stuff up here
}

void test_Config_init(void)
{
    Config_init();

    // Check some defaults defined in config.h and config.c
    TEST_ASSERT_EQUAL_INT(DEFAULT_NETWORK_MAX_CONNS, CONFIG.max_conns);
    TEST_ASSERT_EQUAL_INT(3600, CONFIG.refresh_timeout);
    TEST_ASSERT_EQUAL_INT(NORMAL, CONFIG.mode);
    TEST_ASSERT_EQUAL_INT(0, CONFIG.cache_enabled);
    TEST_ASSERT_EQUAL_STRING(DEFAULT_USER_AGENT, CONFIG.user_agent);
    TEST_ASSERT_NULL(CONFIG.http_username);
    TEST_ASSERT_EQUAL_INT64(-1, (int64_t)CONFIG.cache_min_size);
    TEST_ASSERT_EQUAL_INT64(-1, (int64_t)CONFIG.cache_max_size);
    TEST_ASSERT_EQUAL_INT(0, CONFIG.external_links);
    TEST_ASSERT_EQUAL_INT(0, CONFIG.ignore_anchors);
    TEST_ASSERT_EQUAL_INT(0, CONFIG.advanced_parsing_mode);
    TEST_ASSERT_EQUAL_INT64((int64_t)DEFAULT_MAX_HTML_SIZE,
                            (int64_t)CONFIG.max_html_size);
    TEST_ASSERT_EQUAL_INT(0, CONFIG.same_origin_only);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_Config_init);
    return UNITY_END();
}
