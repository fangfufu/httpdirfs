#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include "../src/config.h"
#include "../src/util.h"

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
    TEST_ASSERT_EQUAL_INT(10, CONFIG.max_conns);
    TEST_ASSERT_EQUAL_INT(3600, CONFIG.refresh_timeout);
    TEST_ASSERT_EQUAL_INT(NORMAL, CONFIG.mode);
    TEST_ASSERT_EQUAL_INT(0, CONFIG.cache_enabled);
    TEST_ASSERT_EQUAL_STRING(DEFAULT_USER_AGENT, CONFIG.user_agent);
    TEST_ASSERT_NULL(CONFIG.http_username);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_Config_init);
    return UNITY_END();
}
