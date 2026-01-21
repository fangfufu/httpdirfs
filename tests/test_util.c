#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include "../src/util.h"

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}

void test_round_div(void) {
    TEST_ASSERT_EQUAL_INT64(3, round_div(10, 3));
    TEST_ASSERT_EQUAL_INT64(4, round_div(11, 3));
    TEST_ASSERT_EQUAL_INT64(2, round_div(4, 2));
    TEST_ASSERT_EQUAL_INT64(0, round_div(0, 5));
    TEST_ASSERT_EQUAL_INT64(1, round_div(1, 2)); // (1 + 1) / 2 = 1. Wait. 0.5 rounds to 1?
                                                 // round_div(1, 2) -> (1 + 1)/2 = 1. Yes.
    TEST_ASSERT_EQUAL_INT64(0, round_div(1, 3)); // (1 + 1)/3 = 0. 0.33 rounds to 0. Correct.
}

void test_path_append(void) {
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
    // Depending on implementation, this might be /home/user//docs or /home/user/docs.
    // The implementation concatenates them directly if no separator is needed.
    // So it will be /home/user//docs.
    TEST_ASSERT_EQUAL_STRING("/home/user//docs", result);
    FREE(result);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_round_div);
    RUN_TEST(test_path_append);
    return UNITY_END();
}
