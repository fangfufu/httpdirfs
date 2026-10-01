#include "../src/config.h"
#include "../src/util.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <unity.h>

void setUp(void)
{
    Config_init();
}

void tearDown(void)
{
    // clean stuff up here
}

void test_path_append(void)
{
    char *result;

    // Case 1: No trailing slash in path, no leading slash in filename
    result = path_append("/home/user", "docs");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("/home/user/docs", result);
    FREE(result);

    // Case 2: Trailing slash in path
    result = path_append("/home/user/", "docs");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("/home/user/docs", result);
    FREE(result);

    // Case 3: Leading slash in filename
    result = path_append("/home/user", "/docs");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("/home/user/docs", result);
    FREE(result);

    // Case 4: Both have slash
    result = path_append("/home/user/", "/docs");
    // The implementation now handles redundant slashes and normalizes them.
    // So it will be /home/user/docs.
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("/home/user/docs", result);
    FREE(result);

    // Case 4b: Multiple leading slashes in filename, trailing slash in path
    result = path_append("/home/user/", "//docs");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("/home/user/docs", result);
    FREE(result);

    // Case 4c: Multiple leading slashes in filename, no trailing slash in path
    result = path_append("/home/user", "//docs");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("/home/user/docs", result);
    FREE(result);
}

void test_generate_md5sum(void)
{
    char *hash1 = generate_md5sum("httpdirfs");
    TEST_ASSERT_NOT_NULL(hash1);
    TEST_ASSERT_EQUAL_STRING("210a6f1303020274c9fa9c51a5538778", hash1);
    FREE(hash1);

    char *hash2 = generate_md5sum("");
    TEST_ASSERT_NOT_NULL(hash2);
    TEST_ASSERT_EQUAL_STRING("d41d8cd98f00b204e9800998ecf8427e", hash2);
    FREE(hash2);
}

void test_str_to_hex(void)
{
    char *hex1 = str_to_hex("A!");
    TEST_ASSERT_NOT_NULL(hex1);
    TEST_ASSERT_EQUAL_STRING("4121", hex1);
    FREE(hex1);

    char *hex2 = str_to_hex("");
    TEST_ASSERT_NOT_NULL(hex2);
    TEST_ASSERT_EQUAL_STRING("", hex2);
    FREE(hex2);
}

void test_generate_salt(void)
{
    char *salt1 = generate_salt();
    char *salt2 = generate_salt();
    TEST_ASSERT_NOT_NULL(salt1);
    TEST_ASSERT_NOT_NULL(salt2);
    TEST_ASSERT_EQUAL_INT(36, strlen(salt1));
    TEST_ASSERT_EQUAL_INT(36, strlen(salt2));
    // Test randomness
    TEST_ASSERT_NOT_EQUAL(0, strcmp(salt1, salt2));
    FREE(salt1);
    FREE(salt2);
}

void test_realloc_size_zero(void)
{
    char *ptr = CALLOC(10, sizeof(char));
    TEST_ASSERT_NOT_NULL(ptr);

    ptr = REALLOC(ptr, 0);
    TEST_ASSERT_NULL(ptr);
}

void test_memory_tracking(void)
{
    // Test CALLOC wrapper
    int *arr = CALLOC(10, sizeof(int));
    TEST_ASSERT_NOT_NULL(arr);
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(0, arr[i]);
        arr[i] = i;
    }

    // Test REALLOC with size expansion
    // Allocate a large filler block to consume adjacent heap space
    int *filler = CALLOC(100000, sizeof(int));
    TEST_ASSERT_NOT_NULL(filler);

    arr = REALLOC(arr, 200000 * sizeof(int));
    TEST_ASSERT_NOT_NULL(arr);
    // Note: relocation may or may not occur; both are valid REALLOC behavior

    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(i, arr[i]);
    }

    // Test STRDUP / STRNDUP
    char *s = STRDUP("httpdirfs_test");
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_STRING("httpdirfs_test", s);

    char *s2 = STRNDUP(s, 9);
    TEST_ASSERT_NOT_NULL(s2);
    TEST_ASSERT_EQUAL_STRING("httpdirfs", s2);

    // Test FREE wrapper
    FREE(filler);
    TEST_ASSERT_NULL(filler);

    FREE(arr);
    TEST_ASSERT_NULL(arr);

    FREE(s);
    TEST_ASSERT_NULL(s);

    FREE(s2);
    TEST_ASSERT_NULL(s2);
}

void test_check_space(void)
{
    // ASCII space -> 1
    TEST_ASSERT_EQUAL_INT(1, check_space(" "));
    TEST_ASSERT_EQUAL_INT(1, check_space("\t"));
    TEST_ASSERT_EQUAL_INT(1, check_space("\n"));
    TEST_ASSERT_EQUAL_INT(1, check_space("\r"));
    TEST_ASSERT_EQUAL_INT(1, check_space("\v"));
    TEST_ASSERT_EQUAL_INT(1, check_space("\f"));

    // UTF-8 non-breaking space (0xC2, 0xA0) -> 2
    TEST_ASSERT_EQUAL_INT(2, check_space("\xc2\xa0"));

    // Non-space character -> 0
    TEST_ASSERT_EQUAL_INT(0, check_space("a"));
    TEST_ASSERT_EQUAL_INT(0, check_space(""));

    // Null pointer -> 0
    TEST_ASSERT_EQUAL_INT(0, check_space(NULL));
}

void test_parse_size_with_suffix(void)
{
    // Valid raw bytes
    TEST_ASSERT_EQUAL_INT64(0, (int64_t)parse_size_with_suffix("0", NULL));
    TEST_ASSERT_EQUAL_INT64(100, (int64_t)parse_size_with_suffix("100", NULL));
    TEST_ASSERT_EQUAL_INT64(2097152,
                            (int64_t)parse_size_with_suffix("2097152", NULL));
    TEST_ASSERT_EQUAL_INT64(500,
                            (int64_t)parse_size_with_suffix("  500  ", NULL));

    // Valid suffixes (case-insensitive)
    TEST_ASSERT_EQUAL_INT64(1024, (int64_t)parse_size_with_suffix("1k", NULL));
    TEST_ASSERT_EQUAL_INT64(524288,
                            (int64_t)parse_size_with_suffix("512K", NULL));
    TEST_ASSERT_EQUAL_INT64(1048576,
                            (int64_t)parse_size_with_suffix("1m", NULL));
    TEST_ASSERT_EQUAL_INT64(2097152,
                            (int64_t)parse_size_with_suffix("2M", NULL));
    TEST_ASSERT_EQUAL_INT64(1073741824LL,
                            (int64_t)parse_size_with_suffix("1g", NULL));
    TEST_ASSERT_EQUAL_INT64(2147483648LL,
                            (int64_t)parse_size_with_suffix("2G", NULL));

    // Invalid inputs
    TEST_ASSERT_EQUAL_INT64(-1, (int64_t)parse_size_with_suffix(NULL, NULL));
    TEST_ASSERT_EQUAL_INT64(-1, (int64_t)parse_size_with_suffix("", NULL));
    TEST_ASSERT_EQUAL_INT64(-1, (int64_t)parse_size_with_suffix("   ", NULL));
    TEST_ASSERT_EQUAL_INT64(-1, (int64_t)parse_size_with_suffix("-1", NULL));
    TEST_ASSERT_EQUAL_INT64(-1, (int64_t)parse_size_with_suffix("-2M", NULL));
    TEST_ASSERT_EQUAL_INT64(-1, (int64_t)parse_size_with_suffix("abc", NULL));
    TEST_ASSERT_EQUAL_INT64(-1, (int64_t)parse_size_with_suffix("2MB", NULL));
    TEST_ASSERT_EQUAL_INT64(-1, (int64_t)parse_size_with_suffix("2X", NULL));
    TEST_ASSERT_EQUAL_INT64(-1, (int64_t)parse_size_with_suffix("1.5M", NULL));
    TEST_ASSERT_EQUAL_INT64(-1, (int64_t)parse_size_with_suffix(
                                    "99999999999999999999999999999G", NULL));
}

static void cleanup_test_mkdir_p(void)
{
    (void)rmdir("/tmp/httpdirfs_test_mkdir_p/sub1/sub2/sub3");
    (void)rmdir("/tmp/httpdirfs_test_mkdir_p/sub1/sub2");
    (void)rmdir("/tmp/httpdirfs_test_mkdir_p/sub1");
    (void)rmdir("/tmp/httpdirfs_test_mkdir_p");
}

void test_mkdir_p(void)
{
    const char *nested = "/tmp/httpdirfs_test_mkdir_p/sub1/sub2/sub3";

    cleanup_test_mkdir_p();

    // NULL path
    TEST_ASSERT_EQUAL_INT(-1, mkdir_p(NULL, 0755));

    // Empty path
    TEST_ASSERT_EQUAL_INT(-1, mkdir_p("", 0755));

    // Create nested directory
    TEST_ASSERT_EQUAL_INT(0, mkdir_p(nested, 0755));

    // Check directory exists
    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, stat(nested, &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));

    // Idempotent: creating already existing directory should return 0
    TEST_ASSERT_EQUAL_INT(0, mkdir_p(nested, 0755));

    // Trailing slash
    char nested_slash[256];
    snprintf(nested_slash, sizeof(nested_slash), "%s/", nested);
    TEST_ASSERT_EQUAL_INT(0, mkdir_p(nested_slash, 0755));

    cleanup_test_mkdir_p();
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_path_append);
    RUN_TEST(test_mkdir_p);
    RUN_TEST(test_generate_md5sum);
    RUN_TEST(test_str_to_hex);
    RUN_TEST(test_generate_salt);
    RUN_TEST(test_realloc_size_zero);
    RUN_TEST(test_memory_tracking);
    RUN_TEST(test_check_space);
    RUN_TEST(test_parse_size_with_suffix);
    return UNITY_END();
}
