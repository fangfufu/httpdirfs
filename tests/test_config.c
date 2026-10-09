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

    // Check some defaults defined in config.c
    TEST_ASSERT_EQUAL_INT(6, CONFIG.max_conns);
    TEST_ASSERT_EQUAL_INT(3600, CONFIG.refresh_timeout);
    TEST_ASSERT_EQUAL_INT(NORMAL, CONFIG.mode);
    TEST_ASSERT_EQUAL_INT(0, CONFIG.cache_enabled);
    TEST_ASSERT_EQUAL_STRING("HTTPDirFS-" VERSION, CONFIG.user_agent);
    TEST_ASSERT_NULL(CONFIG.http_username);
    TEST_ASSERT_EQUAL_INT64(-1, (int64_t)CONFIG.cache_min_size);
    TEST_ASSERT_EQUAL_INT64(-1, (int64_t)CONFIG.cache_max_size);
    TEST_ASSERT_EQUAL_INT(0, CONFIG.allow_external_origin);
    TEST_ASSERT_EQUAL_INT(0, CONFIG.website_mode);
    TEST_ASSERT_EQUAL_INT(0, CONFIG.ignore_anchors);
    TEST_ASSERT_EQUAL_INT64((int64_t)2 * 1024 * 1024,
                            (int64_t)CONFIG.max_html_size);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_Config_init);
    return UNITY_END();
}
