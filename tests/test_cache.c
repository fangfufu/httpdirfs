#include <unity.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "../src/cache.h"
#include "../src/config.h"
#include "../src/util.h"
#include "../src/link.h"

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

void test_ActiveDownload_find(void)
{
    Cache cf = {0};

    // 1. Search in an empty list must return NULL
    TEST_ASSERT_NULL(ActiveDownload_find(&cf, 1024));

    // 2. Construct a mock active downloads linked list
    ActiveDownload ad1 = {.offset = 1024, .ts = NULL, .next = NULL};
    ActiveDownload ad2 = {.offset = 2048, .ts = NULL, .next = &ad1};
    cf.active_dls = &ad2;

    // 3. Verify that matching offsets are correctly resolved
    TEST_ASSERT_EQUAL_PTR(&ad2, ActiveDownload_find(&cf, 2048));
    TEST_ASSERT_EQUAL_PTR(&ad1, ActiveDownload_find(&cf, 1024));

    // 4. Verify that non-existent offsets correctly return NULL
    TEST_ASSERT_NULL(ActiveDownload_find(&cf, 4096));
}

void test_Cache_read_zero_length(void)
{
    Cache cf = {0};
    Link link = {0};
    link.content_length = 0;
    cf.link = &link;
    cf.blksz = 4096;

    char buf[10];
    long res = Cache_read(&cf, buf, sizeof(buf), 0);
    TEST_ASSERT_EQUAL_INT(0, res);
}

void test_Cache_read_past_eof(void)
{
    Cache cf = {0};
    Link link = {0};
    link.content_length = 100;
    cf.link = &link;
    cf.blksz = 4096;

    char buf[10];
    long res = Cache_read(&cf, buf, sizeof(buf), 100);
    TEST_ASSERT_EQUAL_INT(0, res);

    res = Cache_read(&cf, buf, sizeof(buf), 150);
    TEST_ASSERT_EQUAL_INT(0, res);
}

void test_Cache_read_negative_len(void)
{
    /* The new len < 0 guard must reject negative lengths with -EINVAL
     * rather than wrapping the value to a huge size_t. */
    Cache cf = {0};
    Link link = {0};
    link.content_length = 1024;
    cf.link = &link;
    cf.blksz = 4096;

    char buf[64];
    long res = Cache_read(&cf, buf, (off_t)-1, 0);
    TEST_ASSERT_EQUAL_INT(-EINVAL, res);

    res = Cache_read(&cf, buf, (off_t)-1024, 0);
    TEST_ASSERT_EQUAL_INT(-EINVAL, res);
}

void test_Cache_read_null_cf(void)
{
    /* Cache_read must return -EINVAL when passed a NULL cf pointer. */
    char buf[16];
    long res = Cache_read(NULL, buf, sizeof(buf), 0);
    TEST_ASSERT_EQUAL_INT(-EINVAL, res);
}

void test_Cache_read_null_link(void)
{
    /* Cache_read must return -EINVAL when cf->link is NULL. */
    Cache cf = {0};
    cf.link = NULL;

    char buf[16];
    long res = Cache_read(&cf, buf, sizeof(buf), 0);
    TEST_ASSERT_EQUAL_INT(-EINVAL, res);
}

static void cleanup_temp_dir(const char *tmp_cache_dir)
{
    char filepath[512];

    // Remove files
    snprintf(filepath, sizeof(filepath), "%s/meta/file.bin", tmp_cache_dir);
    (void)unlink(filepath);

    snprintf(filepath, sizeof(filepath), "%s/data/file.bin", tmp_cache_dir);
    (void)unlink(filepath);

    // Remove directories
    snprintf(filepath, sizeof(filepath), "%s/meta", tmp_cache_dir);
    (void)rmdir(filepath);

    snprintf(filepath, sizeof(filepath), "%s/data", tmp_cache_dir);
    (void)rmdir(filepath);

    (void)rmdir(tmp_cache_dir);
}

void test_Cache_invalid_zero_length_disk_files(void)
{
    // Set up a temp cache directory
    const char *tmp_cache_dir = "./test_cache_invalidation_dir";
    char meta_dir[256];
    char data_dir[256];
    snprintf(meta_dir, sizeof(meta_dir), "%s/meta", tmp_cache_dir);
    snprintf(data_dir, sizeof(data_dir), "%s/data", tmp_cache_dir);

    // Make sure they exist and are empty
    cleanup_temp_dir(tmp_cache_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir(tmp_cache_dir, 0700));
    TEST_ASSERT_EQUAL_INT(0, mkdir(meta_dir, 0700));
    TEST_ASSERT_EQUAL_INT(0, mkdir(data_dir, 0700));

    // Store old CONFIG.cache_dir
    char *old_cache_dir = CONFIG.cache_dir;
    CONFIG.cache_dir = (char *)tmp_cache_dir;

    // Set up a mock Link with a non-zero content_length
    LinkTable *table = LinkTable_alloc("https://example.com/");
    Link *link = CALLOC(1, sizeof(Link));
    link->type = LINK_FILE;
    link->content_length = 100;
    link->parent_table = table;
    strncpy(link->linkname, "file.bin", NAME_MAX);
    strncpy(link->f_url, "https://example.com/file.bin", PATH_MAX);
    LinkTable_add(table, link);

    // Expose ROOT_LINK_TBL and ROOT_LINK_OFFSET
    LinkTable *old_root_link_tbl = ROOT_LINK_TBL;
    ROOT_LINK_TBL = table;
    int old_root_link_offset = ROOT_LINK_OFFSET;
    ROOT_LINK_OFFSET = 20;

    // Initialize cache system with our temp directory
    CacheSystem_init(tmp_cache_dir, 0);

    // Scenario 1: Metadata file exists but is 0 bytes (empty)
    char meta_filepath[512];
    char data_filepath[512];
    snprintf(meta_filepath, sizeof(meta_filepath), "%s/meta/file.bin",
             tmp_cache_dir);
    snprintf(data_filepath, sizeof(data_filepath), "%s/data/file.bin",
             tmp_cache_dir);

    // Touch both files to make them exist, meta is 0 bytes
    FILE *f_meta = fopen(meta_filepath, "w");
    TEST_ASSERT_NOT_NULL(f_meta);
    fclose(f_meta);

    FILE *f_data = fopen(data_filepath, "w");
    TEST_ASSERT_NOT_NULL(f_data);
    fclose(f_data);

    // Now, call Cache_open. Since meta is empty, it is invalid.
    // Cache_open should detect this invalid/corrupted cache, delete it,
    // and recreate a fresh cache which should succeed and have a correct
    // non-zero size.
    Cache *cf = Cache_open("file.bin");
    TEST_ASSERT_NOT_NULL(cf);
    TEST_ASSERT_NOT_NULL(cf->mfp);
    TEST_ASSERT_NOT_NULL(cf->dfp);

    // Verify that the recreated meta file is now not empty (it has valid
    // metadata written)
    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, fstat(fileno(cf->mfp), &st));
    TEST_ASSERT_TRUE(st.st_size > 0);

    // Close the cache
    Cache_close(cf);

    // Scenario 2: Metadata file contains invalid zero-length/negative size
    // Create invalid metadata file with disk_content_length = 0
    f_meta = fopen(meta_filepath, "w");
    TEST_ASSERT_NOT_NULL(f_meta);
    long bad_time = 0;
    off_t bad_len = 0;
    int bad_blksz = 4096;
    long bad_segbc = 0;
    fwrite(&bad_time, sizeof(long), 1, f_meta);
    fwrite(&bad_len, sizeof(off_t), 1, f_meta);
    fwrite(&bad_blksz, sizeof(int), 1, f_meta);
    fwrite(&bad_segbc, sizeof(long), 1, f_meta);
    fclose(f_meta);

    // Re-open
    cf = Cache_open("file.bin");
    TEST_ASSERT_NOT_NULL(cf);
    TEST_ASSERT_NOT_NULL(cf->mfp);
    TEST_ASSERT_NOT_NULL(cf->dfp);

    // Verify it was again invalidated, recreated, and now has a valid non-zero
    // content_length
    TEST_ASSERT_EQUAL_INT(0, fstat(fileno(cf->mfp), &st));
    TEST_ASSERT_TRUE(st.st_size > 0);

    Cache_close(cf);

    // Cleanup
    ROOT_LINK_TBL = old_root_link_tbl;
    ROOT_LINK_OFFSET = old_root_link_offset;
    LinkTable_free(table);
    CacheSystem_cleanup();
    CONFIG.cache_dir = old_cache_dir;
    cleanup_temp_dir(tmp_cache_dir);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_CacheSystem_get_cache_dir);
    RUN_TEST(test_ActiveDownload_find);
    RUN_TEST(test_Cache_read_zero_length);
    RUN_TEST(test_Cache_read_past_eof);
    RUN_TEST(test_Cache_read_negative_len);
    RUN_TEST(test_Cache_read_null_cf);
    RUN_TEST(test_Cache_read_null_link);
    RUN_TEST(test_Cache_invalid_zero_length_disk_files);
    return UNITY_END();
}
