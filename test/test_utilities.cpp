extern "C" {

#include <util.h>
#include <config.h>

}
#include <gtest/gtest.h>
namespace {

#define TEST_FILE_NAME_LEN 6
    const char *test_file_name = "id_rsa";

#define TEST_FOLDER_NAME_LEN 6
    const char test_folder_name[] = "/abcdefg";

    const char *path_root = "/";
    const char *path_sample1 = "/www";
    const char *path_sample2 = "/www/folder1/";
    const char *path_sample3 = "/www/folder1/folder2";

    TEST(PathAppendTest, PathLengthLessThanMaxPathLen) {

        char *path = path_append(path_root, test_file_name);
        ASSERT_STREQ("/id_rsa", path);
        free(path);

        path = path_append(path_sample1, test_file_name);
        ASSERT_STREQ("/www/id_rsa", path);
        free(path);

        path = path_append(path_sample2, test_file_name);
        ASSERT_STREQ("/www/folder1/id_rsa", path);
        free(path);

        path = path_append(path_sample3, test_file_name);
        ASSERT_STREQ("/www/folder1/folder2/id_rsa", path);
        free(path);
    }

    TEST(PathAppendTest, PathLengthGreaterThanMaxPathLen) {
#define PATH_LEN_4098 4098

        char very_long_path[PATH_LEN_4098] = { 0 };
        char *p = very_long_path;

        /*
         *  MAX_PATH_LEN is a perfect multiple of the length of test_folder_name,
         *  so it would fit perfectly in the 4096 bytes of very_long_path.
         */
        for (int i = 0; i < PATH_LEN_4098; i += TEST_FOLDER_NAME_LEN) {
            memcpy(p, test_folder_name, TEST_FOLDER_NAME_LEN);
            p += TEST_FOLDER_NAME_LEN;
        }

        char *path = path_append(very_long_path, test_file_name);
        ASSERT_NE(nullptr, path);
        ASSERT_EQ(path[MAX_PATH_LEN - 1], 'c');
        ASSERT_EQ(path[MAX_PATH_LEN + 0], '/');
        ASSERT_EQ(path[MAX_PATH_LEN + 1], 'i');
        ASSERT_EQ(path[MAX_PATH_LEN + 6], 'a');
        free(path);
    }
}                               // namespace
