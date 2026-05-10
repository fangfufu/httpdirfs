#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include "../src/util.h"

void setUp(void)
{
    // set stuff up here
}

void tearDown(void)
{
    // clean stuff up here
}

void test_round_div(void)
{
    TEST_ASSERT_EQUAL_INT64(3, round_div(10, 3));
    TEST_ASSERT_EQUAL_INT64(4, round_div(11, 3));
    TEST_ASSERT_EQUAL_INT64(2, round_div(4, 2));
    TEST_ASSERT_EQUAL_INT64(0, round_div(0, 5));
    TEST_ASSERT_EQUAL_INT64(
        1, round_div(1, 2)); // (1 + 1) / 2 = 1. Wait. 0.5 rounds to 1?
                             // round_div(1, 2) -> (1 + 1)/2 = 1. Yes.
    TEST_ASSERT_EQUAL_INT64(
        0, round_div(1, 3)); // (1 + 1)/3 = 0. 0.33 rounds to 0. Correct.
}

void test_path_append(void)
{
    char *result;

    // Case 1: No trailing slash in path, no leading slash in filename
    result = path_append("/home/user", "docs");
    TEST_ASSERT_EQUAL_STRING("/home/user/docs", result);
    FREE(result);

    // Case 2: Trailing slash in path
    result = path_append("/home/user/", "docs");
    TEST_ASSERT_EQUAL_STRING("/home/user/docs", result);
    FREE(result);

    // Case 3: Leading slash in filename
    result = path_append("/home/user", "/docs");
    TEST_ASSERT_EQUAL_STRING("/home/user/docs", result);
    FREE(result);

    // Case 4: Both have slash
    result = path_append("/home/user/", "/docs");
    // Depending on implementation, this might be /home/user//docs or
    // /home/user/docs. The implementation concatenates them directly if no
    // separator is needed. So it will be /home/user//docs.
    TEST_ASSERT_EQUAL_STRING("/home/user//docs", result);
    FREE(result);
}

void test_generate_md5sum(void)
{
    char *hash1 = generate_md5sum("httpdirfs");
    TEST_ASSERT_EQUAL_STRING("210a6f1303020274c9fa9c51a5538778", hash1);
    FREE(hash1);

    char *hash2 = generate_md5sum("");
    TEST_ASSERT_EQUAL_STRING("d41d8cd98f00b204e9800998ecf8427e", hash2);
    FREE(hash2);
}

void test_str_to_hex(void)
{
    char *hex = str_to_hex("A!");
    TEST_ASSERT_EQUAL_STRING("4121", hex);
    FREE(hex);
}

void test_generate_salt(void)
{
    char *salt1 = generate_salt();
    char *salt2 = generate_salt();
    TEST_ASSERT_EQUAL_INT(36, strlen(salt1));
    TEST_ASSERT_EQUAL_INT(36, strlen(salt2));
    // Test randomness
    TEST_ASSERT_NOT_EQUAL(0, strcmp(salt1, salt2));
    FREE(salt1);
    FREE(salt2);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_round_div);
    RUN_TEST(test_path_append);
    RUN_TEST(test_generate_md5sum);
    RUN_TEST(test_str_to_hex);
    RUN_TEST(test_generate_salt);
    return UNITY_END();
}
