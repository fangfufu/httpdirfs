#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include "../src/cache.h"
#include "../src/config.h"
#include "../src/util.h"

void setUp(void)
{
    Config_init();
}

void tearDown(void)
{
    // clean stuff up here
}

void test_CacheSystem_get_cache_dir(void)
{
    char *dir;

    // 1. Test when CONFIG.cache_dir is explicitly set
    CONFIG.cache_dir = "/tmp/explicit_cache";
    dir = CacheSystem_get_cache_dir();
    TEST_ASSERT_NOT_NULL(dir);
    TEST_ASSERT_EQUAL_STRING("/tmp/explicit_cache", dir);
    // Not dynamically allocated in cache.c when CONFIG.cache_dir is used
    CONFIG.cache_dir = NULL;

    // 2. Test fallback to XDG_CACHE_HOME
    setenv("XDG_CACHE_HOME", "/tmp/xdg_cache", 1);
    dir = CacheSystem_get_cache_dir();
    TEST_ASSERT_NOT_NULL(dir);
    TEST_ASSERT_EQUAL_STRING("/tmp/xdg_cache", dir);
    FREE(dir);
    unsetenv("XDG_CACHE_HOME");

    // 3. Test fallback to HOME/.cache
    setenv("HOME", "/tmp/my_home", 1);
    dir = CacheSystem_get_cache_dir();
    TEST_ASSERT_NOT_NULL(dir);
    TEST_ASSERT_EQUAL_STRING("/tmp/my_home/.cache", dir);
    FREE(dir);
    unsetenv("HOME");

    // 4. Test fallback when neither is set
    dir = CacheSystem_get_cache_dir();
    TEST_ASSERT_NOT_NULL(dir);
    char *expected_cur_dir = REALPATH("./", NULL);
    TEST_ASSERT_NOT_NULL(expected_cur_dir);
    char *expected_dir = path_append(expected_cur_dir, ".cache");
    TEST_ASSERT_NOT_NULL(expected_dir);
    TEST_ASSERT_EQUAL_STRING(expected_dir, dir);
    FREE(expected_cur_dir);
    FREE(expected_dir);
    FREE(dir);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_CacheSystem_get_cache_dir);
    return UNITY_END();
}
