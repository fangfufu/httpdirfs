extern "C" {

#include <util.h>
#include <config.h>

}
#include <gtest/gtest.h>
namespace {

    const char test_file_name[] = "id_rsa";

    TEST(PathAppendTest, PathLengthLessThanMaxPathLen) {
        const char *path_root = "/";
        const char *path_sample1 = "/www";
        const char *path_sample2 = "/www/folder1/";
        const char *path_sample3 = "/www/folder1/folder2";

        char *path = path_append(path_root, test_file_name);
        ASSERT_STREQ("/id_rsa", path);
        FREE(path);

        path = path_append(path_sample1, test_file_name);
        ASSERT_STREQ("/www/id_rsa", path);
        FREE(path);

        path = path_append(path_sample2, test_file_name);
        ASSERT_STREQ("/www/folder1/id_rsa", path);
        FREE(path);

        path = path_append(path_sample3, test_file_name);
        ASSERT_STREQ("/www/folder1/folder2/id_rsa", path);
        FREE(path);
    }

    TEST(PathAppendTest, PathLengthGreaterThanMaxPathLen) {
        const char test_folder_name[] = "/abcdefg";
        const int test_folder_name_len = 8;
        const int path_len_4098 = 4098;
        char very_long_path[path_len_4098] = { 0 };
        char *p = very_long_path;

        /*
         *  MAX_PATH_LEN is an integer multiple of test_folder_name_len,
         *  so it would fit perfectly in the 4096 bytes of very_long_path.
         *  According to strncat manual, if the length of the C string in
         *  source is less than num, only the content up to the terminating
         *  null-character is copied.
         */
        for (int i = 0; i < path_len_4098; i += test_folder_name_len) {
            strncat(p, test_folder_name, test_folder_name_len + 1);
            p += test_folder_name_len;
        }

        char *path = path_append(very_long_path, test_file_name);
        ASSERT_NE(nullptr, path);
        ASSERT_EQ(path[MAX_PATH_LEN - 1], 'g');
        ASSERT_EQ(path[MAX_PATH_LEN + 0], '/');
        ASSERT_EQ(path[MAX_PATH_LEN + 1], 'i');
        ASSERT_EQ(path[MAX_PATH_LEN + 6], 'a');
        FREE(path);
    }
}                               // namespace
