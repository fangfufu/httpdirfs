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
        const std::string test_folder("/abcdefg");
        const int test_folder_len = test_folder.size();
        const int path_len_4098 = 4098;
        std::string very_long_path;

        /*
         *  MAX_PATH_LEN is an integer multiple of test_folder_len,
         *  so it would fit perfectly in 4096 bytes,
         */
        for (int i = path_len_4098 / test_folder_len; i != 0; i--)
            very_long_path += test_folder;

        char *path = path_append(very_long_path.c_str(), test_file_name);
        ASSERT_NE(nullptr, path);
        ASSERT_EQ(path[MAX_PATH_LEN - 1], 'g');
        ASSERT_EQ(path[MAX_PATH_LEN + 0], '/');
        ASSERT_EQ(path[MAX_PATH_LEN + 1], 'i');
        ASSERT_EQ(path[MAX_PATH_LEN + 6], 'a');
        FREE(path);
    }
}                               // namespace
