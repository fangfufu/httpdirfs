#include "../src/cache.h"
#include "../src/config.h"
#include "../src/link.h"
#include "../src/util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unity.h>
#include <unistd.h>

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
    FREE(dir);
    CONFIG.cache_dir = NULL;

    // 2. Test fallback to XDG_CACHE_HOME
    setenv("XDG_CACHE_HOME", "/tmp/xdg_cache", 1);
    dir = CacheSystem_get_cache_dir();
    TEST_ASSERT_NOT_NULL(dir);
    TEST_ASSERT_EQUAL_STRING("/tmp/xdg_cache", dir);
    FREE(dir);
    unsetenv("XDG_CACHE_HOME");

    // 3. Test that an empty or relative XDG_CACHE_HOME is treated as unset
    setenv("HOME", "/tmp/my_home", 1);
    setenv("XDG_CACHE_HOME", "", 1);
    dir = CacheSystem_get_cache_dir();
    TEST_ASSERT_NOT_NULL(dir);
    TEST_ASSERT_EQUAL_STRING("/tmp/my_home/.cache", dir);
    FREE(dir);
    setenv("XDG_CACHE_HOME", "relative/xdg_cache", 1);
    dir = CacheSystem_get_cache_dir();
    TEST_ASSERT_NOT_NULL(dir);
    TEST_ASSERT_EQUAL_STRING("/tmp/my_home/.cache", dir);
    FREE(dir);
    unsetenv("XDG_CACHE_HOME");

    // 4. Test fallback to HOME/.cache
    dir = CacheSystem_get_cache_dir();
    TEST_ASSERT_NOT_NULL(dir);
    TEST_ASSERT_EQUAL_STRING("/tmp/my_home/.cache", dir);
    FREE(dir);
    unsetenv("HOME");

    // 5. Test fallback when neither is set
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

static int ntfw_cb(const char *fpath, const struct stat *sb, int typeflag,
                   struct FTW *ftwbuf)
{
    /*
     * ftwbuf is intentionally non-const: glibc's nftw() callback type
     * (__nftw_func_t) requires a non-const 4th argument, so the S995
     * pointer-to-const suggestion cannot be applied here.
     */
    (void)sb;
    (void)typeflag;
    (void)ftwbuf;
    return remove(fpath);
}

static void cleanup_temp_dir(const char *tmp_cache_dir)
{
    char filepath[512];
    DIR *dir = opendir(tmp_cache_dir);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
                continue;
            }
            snprintf(filepath, sizeof(filepath), "%s/%s", tmp_cache_dir,
                     entry->d_name);
            /*
             * Recursively remove origin subdirectories, which contain the
             * shard directories ("<origin>/<ab>/<hash>" layout)
             */
            nftw(filepath, ntfw_cb, 32, FTW_DEPTH | FTW_PHYS);
        }
        closedir(dir);
    }
    (void)rmdir(tmp_cache_dir);
}

static LinkTable *setup_mock_link_table(const char *link_name)
{
    LinkTable *table = LinkTable_alloc("https://example.com/");
    Link *link = CALLOC(1, sizeof(Link));
    link->type = LINK_FILE;
    link->content_length = 100;
    link->parent_table = table;
    strncpy(link->linkname, link_name, NAME_MAX);
    snprintf(link->f_url, PATH_MAX, "https://example.com/%s", link_name);
    LinkTable_add(table, link);
    return table;
}

static void setup_temp_cache_dir(const char *tmp_cache_dir)
{
    cleanup_temp_dir(tmp_cache_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir(tmp_cache_dir, S_IRWXU));
}

void test_Cache_invalid_zero_length_disk_files(void)
{
    char tmp_cache_dir[] = "./test_cache_invalidation_dir";
    setup_temp_cache_dir(tmp_cache_dir);

    char *old_cache_dir = CONFIG.cache_dir;
    CONFIG.cache_dir = tmp_cache_dir;

    LinkTable *table = setup_mock_link_table("file.bin");

    LinkTable *old_root_link_tbl = ROOT_LINK_TBL;
    ROOT_LINK_TBL = table;

    CacheSystem_init("https://example.com/");

    char *cache_key = url_to_cache_path("https://example.com/file.bin");
    TEST_ASSERT_NOT_NULL(cache_key);
    char container_filepath[512];
    snprintf(container_filepath, sizeof(container_filepath), "%s/%s", CACHE_DIR,
             cache_key);
    char shard_dir[512];
    snprintf(shard_dir, sizeof(shard_dir), "%s/%.2s", CACHE_DIR, cache_key);
    TEST_ASSERT_EQUAL_INT(0, mkdir_p(shard_dir, S_IRWXU));

    // Scenario 1: the container file exists but is 0 bytes (it was created
    // but the header was never written)
    FILE *f = fopen(container_filepath, "w");
    TEST_ASSERT_NOT_NULL(f);
    if (f == NULL) {
        return;
    }
    fclose(f);

    // Cache_open should detect this invalid container, delete it,
    // and recreate a fresh cache which should succeed and have a valid
    // non-zero size.
    Cache *cf = Cache_open("file.bin");
    TEST_ASSERT_NOT_NULL(cf);
    TEST_ASSERT_NOT_NULL(cf->fp);

    // Verify that the recreated container file now has a valid header
    struct stat st;
    int fd = fileno(cf->fp);
    TEST_ASSERT_TRUE(fd >= 0);
    if (fd < 0) {
        return;
    }
    TEST_ASSERT_EQUAL_INT(0, fstat(fd, &st));
    TEST_ASSERT_TRUE(st.st_size > CACHE_HEADER_SIZE);

    // Close the cache
    Cache_close(cf);

    // Scenario 2: the container file has a header with invalid
    // zero-length content_length / segbc fields
    f = fopen(container_filepath, "w");
    TEST_ASSERT_NOT_NULL(f);
    if (f == NULL) {
        return;
    }
    CacheHeader bad_hdr;
    memset(&bad_hdr, 0, sizeof(bad_hdr));
    bad_hdr.magic = CACHE_MAGIC;
    bad_hdr.version = CACHE_VERSION;
    bad_hdr.content_length = 0;
    bad_hdr.blksz = CONFIG.data_blksz;
    bad_hdr.segbc = 0;
    fwrite(&bad_hdr, 1, CACHE_HEADER_SIZE, f);
    fclose(f);

    // Re-open: it must be invalidated, recreated, and now valid
    cf = Cache_open("file.bin");
    TEST_ASSERT_NOT_NULL(cf);
    TEST_ASSERT_NOT_NULL(cf->fp);

    // Verify it was again invalidated, recreated, and now has a valid
    // non-zero size
    fd = fileno(cf->fp);
    TEST_ASSERT_TRUE(fd >= 0);
    if (fd < 0) {
        return;
    }
    TEST_ASSERT_EQUAL_INT(0, fstat(fd, &st));
    TEST_ASSERT_TRUE(st.st_size > CACHE_HEADER_SIZE);

    Cache_close(cf);

    // Cleanup
    ROOT_LINK_TBL = old_root_link_tbl;
    LinkTable_free(table);
    CacheSystem_cleanup();
    CONFIG.cache_dir = old_cache_dir;
    FREE(cache_key);
    cleanup_temp_dir(tmp_cache_dir);
}

void test_Cache_alloc_num_bg_workers(void)
{
    // Mock the external global CONFIG
    int old_max_conns = CONFIG.max_conns;

    char tmp_cache_dir[] = "./test_cache_bg_workers_dir";
    setup_temp_cache_dir(tmp_cache_dir);

    char *old_cache_dir = CONFIG.cache_dir;
    CONFIG.cache_dir = tmp_cache_dir;

    LinkTable *table = setup_mock_link_table("dummy.bin");

    LinkTable *old_root_link_tbl = ROOT_LINK_TBL;
    ROOT_LINK_TBL = table;

    CacheSystem_init("https://example.com/");

    int test_conns[] = {10, 4, 1};
    for (int i = 0; i < 3; i++) {
        CONFIG.max_conns = test_conns[i];
        Cache *cf = Cache_open("dummy.bin");
        TEST_ASSERT_NOT_NULL(cf);
        int expected = CONFIG.max_conns / 2;
        if (expected <= 0) {
            expected = 1;
        }
        TEST_ASSERT_EQUAL_INT(expected, cf->num_bg_workers);
        Cache_close(cf);
    }

    // Cleanup
    ROOT_LINK_TBL = old_root_link_tbl;
    LinkTable_free(table);
    CacheSystem_cleanup();
    CONFIG.cache_dir = old_cache_dir;
    cleanup_temp_dir(tmp_cache_dir);
    CONFIG.max_conns = old_max_conns;
}

void test_Cache_free_active_downloads(void)
{
    char tmp_cache_dir[] = "./test_cache_free_ad_dir";
    setup_temp_cache_dir(tmp_cache_dir);

    char *old_cache_dir = CONFIG.cache_dir;
    CONFIG.cache_dir = tmp_cache_dir;

    LinkTable *table = setup_mock_link_table("dummy.bin");

    LinkTable *old_root_link_tbl = ROOT_LINK_TBL;
    ROOT_LINK_TBL = table;

    CacheSystem_init("https://example.com/");

    Cache *cf = Cache_open("dummy.bin");
    TEST_ASSERT_NOT_NULL(cf);

    // Under the dl_lock, manually add mock ActiveDownload nodes to test
    // Cache_free's teardown path
    PTHREAD_MUTEX_LOCK(&cf->dl_lock);

    ActiveDownload *ad1 = CALLOC(1, sizeof(ActiveDownload));
    ad1->offset = 1024;
    ad1->refcount = 1;
    PTHREAD_COND_INIT(&ad1->cond, NULL);

    ActiveDownload *ad2 = CALLOC(1, sizeof(ActiveDownload));
    ad2->offset = 2048;
    ad2->refcount = 2; // Extra reference to trigger the warning/no-free path
    PTHREAD_COND_INIT(&ad2->cond, NULL);

    ad2->next = ad1;
    cf->active_dls = ad2;

    PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);

    // Call Cache_close to trigger Cache_free
    Cache_close(cf);

    // Since ad2 had a refcount of 2, Cache_free only decremented its refcount
    // to 1 and did not free it. It is therefore safe and valid to inspect ad2
    // here without risking use-after-free.
    TEST_ASSERT_EQUAL_INT(1, ad2->refcount);

    // Clean up ad2 manually to prevent leak warnings
    PTHREAD_COND_DESTROY(&ad2->cond);
    FREE(ad2);

    // Cleanup
    ROOT_LINK_TBL = old_root_link_tbl;
    LinkTable_free(table);
    CacheSystem_cleanup();
    CONFIG.cache_dir = old_cache_dir;
    cleanup_temp_dir(tmp_cache_dir);
}

struct WaiterThreadArgs {
    Cache *cf;
    char *buf;
    off_t len;
    off_t offset;
    long expected_res;
};

static void *waiter_thread_func(void *arg)
{
    struct WaiterThreadArgs *args = (struct WaiterThreadArgs *)arg;
    long res = Cache_read(args->cf, args->buf, args->len, args->offset);
    args->expected_res = res;
    return NULL;
}

void test_Cache_free_active_downloads_with_waiters(void)
{
    char tmp_cache_dir[] = "./test_cache_free_ad_wait_dir";
    setup_temp_cache_dir(tmp_cache_dir);

    char *old_cache_dir = CONFIG.cache_dir;
    CONFIG.cache_dir = tmp_cache_dir;

    LinkTable *table = setup_mock_link_table("dummy.bin");

    LinkTable *old_root_link_tbl = ROOT_LINK_TBL;
    ROOT_LINK_TBL = table;

    CacheSystem_init("https://example.com/");

    Cache *cf = Cache_open("dummy.bin");
    TEST_ASSERT_NOT_NULL(cf);

    // Manually inject an active download so any reader will block
    PTHREAD_MUTEX_LOCK(&cf->dl_lock);
    ActiveDownload *ad = CALLOC(1, sizeof(ActiveDownload));
    ad->offset = 0;
    ad->refcount = 1;
    PTHREAD_COND_INIT(&ad->cond, NULL);
    cf->active_dls = ad;
    PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);

    // Spawn waiter thread
    pthread_t thread;
    char buf[64];
    struct WaiterThreadArgs args = {.cf = cf,
                                    .buf = buf,
                                    .len = sizeof(buf),
                                    .offset = 0,
                                    .expected_res = 0};

    TEST_ASSERT_EQUAL_INT(
        0, pthread_create(&thread, NULL, waiter_thread_func, &args));

    // Wait until the reader thread registers as a waiter
    struct timespec start_time;
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    while (1) {
        PTHREAD_MUTEX_LOCK(&cf->dl_lock);
        int has_waiter = (cf->waiters == 1);
        PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
        if (has_waiter) {
            break;
        }
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double elapsed
            = (double)(current_time.tv_sec - start_time.tv_sec)
              + ((double)(current_time.tv_nsec - start_time.tv_nsec) / 1e9);
        if (elapsed >= 5.0) {
            TEST_FAIL_MESSAGE(
                "Timed out waiting for reader to register as waiter");
        }
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000};
        nanosleep(&delay, NULL);
    }

    // Call Cache_close which triggers Cache_free, setting shutting_down to 1,
    // waking all waiters and joining them before tearing down
    Cache_close(cf);

    // Join the helper thread
    TEST_ASSERT_EQUAL_INT(0, pthread_join(thread, NULL));

    // The read must have been aborted with -EIO due to shutdown
    TEST_ASSERT_EQUAL_INT(-EIO, args.expected_res);

    // Cleanup
    ROOT_LINK_TBL = old_root_link_tbl;
    LinkTable_free(table);
    CacheSystem_cleanup();
    CONFIG.cache_dir = old_cache_dir;
    cleanup_temp_dir(tmp_cache_dir);
}

/* ========================================================================= */
/* Unified single-file cache architecture tests                              */
/* ========================================================================= */

void test_cache_path_derivation(void)
{
    const char *url = "https://example.com/test-item";
    char *path = url_to_cache_path(url);
    TEST_ASSERT_NOT_NULL(path);

    /*
     * 1-level hash sharding: "<first 2 hex of md5>/<full 32-char md5>"
     */
    TEST_ASSERT_EQUAL_INT(35, (int)strlen(path));
    TEST_ASSERT_EQUAL_CHAR('/', path[2]);
    char *hash = generate_md5sum(url);
    TEST_ASSERT_NOT_NULL(hash);
    TEST_ASSERT_EQUAL_INT(0, strncmp(path, hash, 2));
    TEST_ASSERT_EQUAL_INT(0, strncmp(path + 3, hash, 32));
    FREE(hash);

    /*
     * Same URL always derives the same path, different URLs derive
     * different paths.
     */
    char *path_again = url_to_cache_path(url);
    TEST_ASSERT_EQUAL_STRING(path, path_again);
    FREE(path_again);

    char *other = url_to_cache_path("https://example.com/test-item/child.bin");
    TEST_ASSERT_NOT_EQUAL(0, strcmp(path, other));
    FREE(other);

    TEST_ASSERT_NULL(url_to_cache_path(NULL));
    FREE(path);
}

void test_container_file_create_open(void)
{
    char tmp_cache_dir[] = "./test_container_create_open_dir";
    setup_temp_cache_dir(tmp_cache_dir);

    char *old_cache_dir = CONFIG.cache_dir;
    CONFIG.cache_dir = tmp_cache_dir;

    LinkTable *table = LinkTable_alloc("https://example.com/");
    Link *link = CALLOC(1, sizeof(Link));
    link->type = LINK_FILE;
    link->content_length = (size_t)1 << 20;
    link->time = 1000000;
    link->parent_table = table;
    strncpy(link->linkname, "test-item", NAME_MAX);
    snprintf(link->f_url, PATH_MAX, "https://example.com/test-item");
    LinkTable_add(table, link);

    LinkTable *old_root_link_tbl = ROOT_LINK_TBL;
    ROOT_LINK_TBL = table;
    CacheSystem_init("https://example.com/");

    TEST_ASSERT_EQUAL_INT(0, Cache_create("test-item"));

    char *cache_key = url_to_cache_path(link->f_url);
    TEST_ASSERT_NOT_NULL(cache_key);
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", CACHE_DIR, cache_key);

    int fd = open(full_path, O_RDONLY);
    TEST_ASSERT_TRUE(fd >= 0);
    if (fd < 0) {
        return;
    }

    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, fstat(fd, &st));
    TEST_ASSERT_EQUAL_INT((int)(CACHE_PAGE_SIZE + ((off_t)1 << 20)),
                          (int)st.st_size);
    /*
     * Sparse allocation: only the (single) header page is actually
     * allocated, the 1 MiB payload region is not.
     *
     * Physical block accounting for ftruncate-created dataless regions
     * is filesystem-dependent: ext4 reports them as unallocated, while
     * macOS (APFS) reports the logical size in st_blocks. Only assert
     * where the filesystem accounts sparse regions (Linux).
     */
#ifdef __linux__
    TEST_ASSERT_TRUE((uintmax_t)st.st_blocks * 512 < (uintmax_t)st.st_size);
#endif

    FILE *f = fdopen(fd, "r");
    TEST_ASSERT_NOT_NULL(f);
    if (f == NULL) {
        close(fd);
        return;
    }
    CacheHeader hdr;
    TEST_ASSERT_EQUAL_INT(CACHE_HEADER_SIZE,
                          (int)fread(&hdr, 1, CACHE_HEADER_SIZE, f));
    TEST_ASSERT_EQUAL_UINT32(CACHE_MAGIC, hdr.magic);
    TEST_ASSERT_EQUAL_UINT16(CACHE_VERSION, hdr.version);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(link->f_url), hdr.url_len);
    TEST_ASSERT_EQUAL_INT(0, hdr.header_size % CACHE_PAGE_SIZE);
    TEST_ASSERT_EQUAL_UINT32(0, hdr.http_header_len);

    off_t expected_segbc
        = (((off_t)1 << 20) / CONFIG.data_blksz)
          + ((((off_t)1 << 20) % CONFIG.data_blksz != 0) ? 1 : 0);
    size_t raw_meta_size = CACHE_HEADER_SIZE + strlen(link->f_url) + 1 + 0
                           + (size_t)expected_segbc;
    size_t expected_header_size = (raw_meta_size + CACHE_PAGE_SIZE - 1)
                                  & ~(size_t)(CACHE_PAGE_SIZE - 1);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)expected_header_size, hdr.header_size);
    TEST_ASSERT_EQUAL_INT64((int64_t)((off_t)1 << 20), hdr.content_length);
    TEST_ASSERT_EQUAL_INT((int)expected_segbc, hdr.segbc);
    TEST_ASSERT_EQUAL_INT(CONFIG.data_blksz, hdr.blksz);
    TEST_ASSERT_EQUAL_INT64(1000000, hdr.remote_mtime);
    TEST_ASSERT_TRUE(hdr.cache_time > 0);
    TEST_ASSERT_TRUE(hdr.cache_time <= (int64_t)time(NULL));

    /*
     * The canonical source URL string follows the fixed header.
     */
    TEST_ASSERT_EQUAL_INT(0, fseek(f, (long)CACHE_HEADER_SIZE, SEEK_SET));
    char disk_url[PATH_MAX + 1] = {0};
    TEST_ASSERT_EQUAL_INT((int)strlen(link->f_url),
                          (int)fread(disk_url, 1, strlen(link->f_url), f));
    TEST_ASSERT_EQUAL_STRING(link->f_url, disk_url);
    fclose(f);

    /*
     * Cache_open reads the container back: single stat, magic + version
     * validation, header_size and bitmap loaded.
     */
    Cache *cf = Cache_open("test-item");
    TEST_ASSERT_NOT_NULL(cf);
    TEST_ASSERT_EQUAL_INT((int)expected_header_size, (int)cf->header_size);
    TEST_ASSERT_EQUAL_INT((int)expected_segbc, (int)cf->segbc);
    TEST_ASSERT_NOT_NULL(cf->seg);
    TEST_ASSERT_EQUAL_UINT8(0, cf->seg[0]);
    Cache_close(cf);

    // Cleanup
    ROOT_LINK_TBL = old_root_link_tbl;
    LinkTable_free(table);
    CacheSystem_cleanup();
    CONFIG.cache_dir = old_cache_dir;
    FREE(cache_key);
    cleanup_temp_dir(tmp_cache_dir);
}

void test_container_html_parse_on_the_fly(void)
{
    char tmp_cache_dir[] = "./test_container_html_dir";
    setup_temp_cache_dir(tmp_cache_dir);

    char *old_cache_dir = CONFIG.cache_dir;
    CONFIG.cache_dir = tmp_cache_dir;

    const char *url = "https://example.com/test-item";
    const char *html = "<html><body>\n"
                       "<a href=\"child.bin\">child.bin</a>\n"
                       "<a href=\"subdir/\">subdir/</a>\n"
                       "</body></html>";
    const char *http_header = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/html\r\n"
                              "\r\n";

    CacheSystem_init("https://example.com/");

    TEST_ASSERT_EQUAL_INT(0, CacheContainer_write(url, html, strlen(html),
                                                  http_header,
                                                  strlen(http_header)));

    char *cache_key = url_to_cache_path(url);
    TEST_ASSERT_NOT_NULL(cache_key);
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", CACHE_DIR, cache_key);

    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, stat(full_path, &st));

    FILE *f = fopen(full_path, "r");
    TEST_ASSERT_NOT_NULL(f);
    if (f == NULL) {
        return;
    }
    CacheHeader hdr;
    TEST_ASSERT_EQUAL_INT(CACHE_HEADER_SIZE,
                          (int)fread(&hdr, 1, CACHE_HEADER_SIZE, f));
    TEST_ASSERT_EQUAL_UINT32(CACHE_MAGIC, hdr.magic);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(http_header),
                             hdr.http_header_len);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(html),
                             (uint32_t)hdr.content_length);
    TEST_ASSERT_TRUE(hdr.flags & CACHE_FLAG_IS_COMPLETE);
    TEST_ASSERT_EQUAL_INT(0, hdr.header_size % CACHE_PAGE_SIZE);

    /*
     * Raw HTTP response headers are stored at the http_header_offset.
     */
    off_t http_header_offset = (off_t)(CACHE_HEADER_SIZE + hdr.url_len + 1);
    TEST_ASSERT_EQUAL_INT(0, fseeko(f, http_header_offset, SEEK_SET));
    char hdrbuf[128] = {0};
    TEST_ASSERT_EQUAL_INT((int)strlen(http_header),
                          (int)fread(hdrbuf, 1, strlen(http_header), f));
    TEST_ASSERT_EQUAL_MEMORY(http_header, hdrbuf, strlen(http_header));

    /*
     * The payload starts at the 4KB-aligned header_size.
     */
    TEST_ASSERT_EQUAL_INT(0, fseeko(f, (off_t)hdr.header_size, SEEK_SET));
    char *raw_payload = CALLOC(1, strlen(html) + 1);
    TEST_ASSERT_EQUAL_INT((int)strlen(html),
                          (int)fread(raw_payload, 1, strlen(html), f));
    TEST_ASSERT_EQUAL_MEMORY(html, raw_payload, strlen(html));
    fclose(f);
    FREE(raw_payload);

    /*
     * Re-read through the public reader: the container is fresh, so the
     * raw payload and headers come back for an on-the-fly re-parse.
     */
    char *out_payload = NULL;
    size_t out_payload_len = 0;
    char *out_header = NULL;
    size_t out_header_len = 0;
    TEST_ASSERT_EQUAL_INT(1, CacheContainer_read(url, &out_payload,
                                                 &out_payload_len, &out_header,
                                                 &out_header_len));
    TEST_ASSERT_EQUAL_INT((int)strlen(html), (int)out_payload_len);
    TEST_ASSERT_EQUAL_MEMORY(html, out_payload, strlen(html));
    TEST_ASSERT_EQUAL_INT((int)strlen(http_header), (int)out_header_len);
    TEST_ASSERT_EQUAL_MEMORY(http_header, out_header, strlen(http_header));

    /*
     * Regenerate the LinkTable in memory via the Gumbo parser: zero
     * byte-shifting or file conversion.
     */
    LinkTable *tbl = LinkTable_alloc(url);
    LinkTable_parse_html(tbl, url, out_payload);
    TEST_ASSERT_TRUE(tbl->size > 1);
    int found_child = 0;
    int found_subdir = 0;
    for (int i = 1; i < tbl->size; i++) {
        if (!strcmp(tbl->links[i]->linkname, "child.bin")) {
            found_child = 1;
        }
        if (!strcmp(tbl->links[i]->linkname, "subdir")) {
            found_subdir = 1;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, found_child);
    TEST_ASSERT_EQUAL_INT(1, found_subdir);
    LinkTable_free(tbl);
    FREE(out_payload);
    FREE(out_header);

    // Cleanup
    CacheSystem_cleanup();
    CONFIG.cache_dir = old_cache_dir;
    FREE(cache_key);
    cleanup_temp_dir(tmp_cache_dir);
}

void test_container_timestamps(void)
{
    char tmp_cache_dir[] = "./test_container_ts_dir";
    setup_temp_cache_dir(tmp_cache_dir);

    char *old_cache_dir = CONFIG.cache_dir;
    CONFIG.cache_dir = tmp_cache_dir;

    LinkTable *table = setup_mock_link_table("test-item");
    Link *link = table->links[1];
    link->time = 1000000;

    LinkTable *old_root_link_tbl = ROOT_LINK_TBL;
    ROOT_LINK_TBL = table;
    CacheSystem_init("https://example.com/");

    char *cache_key = url_to_cache_path(link->f_url);
    TEST_ASSERT_NOT_NULL(cache_key);
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", CACHE_DIR, cache_key);

    /*
     * Create the container, then tamper with cache_time to simulate a
     * download older than CONFIG.refresh_timeout.
     */
    TEST_ASSERT_EQUAL_INT(0, Cache_create("test-item"));

    FILE *f = fopen(full_path, "r+");
    TEST_ASSERT_NOT_NULL(f);
    if (f == NULL) {
        return;
    }
    CacheHeader hdr;
    TEST_ASSERT_EQUAL_INT(CACHE_HEADER_SIZE,
                          (int)fread(&hdr, 1, CACHE_HEADER_SIZE, f));
    hdr.cache_time = (int64_t)time(NULL) - CONFIG.refresh_timeout - 10;
    TEST_ASSERT_EQUAL_INT(0, fseek(f, 0, SEEK_SET));
    TEST_ASSERT_EQUAL_INT(CACHE_HEADER_SIZE,
                          (int)fwrite(&hdr, 1, CACHE_HEADER_SIZE, f));
    fclose(f);

    /*
     * Aged container whose remote metadata still matches the live link:
     * the container must be KEPT (its downloaded segments are still
     * valid), not truncated and re-downloaded.
     */
    Cache *cf = Cache_open("test-item");
    TEST_ASSERT_NOT_NULL(cf);
    Cache_close(cf);

    f = fopen(full_path, "r");
    TEST_ASSERT_NOT_NULL(f);
    if (f == NULL) {
        return;
    }
    TEST_ASSERT_EQUAL_INT(CACHE_HEADER_SIZE,
                          (int)fread(&hdr, 1, CACHE_HEADER_SIZE, f));
    fclose(f);
    TEST_ASSERT_TRUE((int64_t)time(NULL) - hdr.cache_time
                     > CONFIG.refresh_timeout);
    TEST_ASSERT_EQUAL_INT64(1000000, hdr.remote_mtime);

    /*
     * Deterministic timestamp invalidation: when the remote metadata is
     * unknown (remote_mtime not recorded), the expired container must be
     * detected via cache_time (not filesystem timestamps) and a fresh
     * container with a current cache_time must be created.
     */
    f = fopen(full_path, "r+");
    TEST_ASSERT_NOT_NULL(f);
    if (f == NULL) {
        return;
    }
    TEST_ASSERT_EQUAL_INT(CACHE_HEADER_SIZE,
                          (int)fread(&hdr, 1, CACHE_HEADER_SIZE, f));
    hdr.cache_time = (int64_t)time(NULL) - CONFIG.refresh_timeout - 10;
    hdr.remote_mtime = 0;
    TEST_ASSERT_EQUAL_INT(0, fseek(f, 0, SEEK_SET));
    TEST_ASSERT_EQUAL_INT(CACHE_HEADER_SIZE,
                          (int)fwrite(&hdr, 1, CACHE_HEADER_SIZE, f));
    fclose(f);

    cf = Cache_open("test-item");
    TEST_ASSERT_NOT_NULL(cf);
    Cache_close(cf);

    f = fopen(full_path, "r");
    TEST_ASSERT_NOT_NULL(f);
    if (f == NULL) {
        return;
    }
    TEST_ASSERT_EQUAL_INT(CACHE_HEADER_SIZE,
                          (int)fread(&hdr, 1, CACHE_HEADER_SIZE, f));
    fclose(f);
    TEST_ASSERT_TRUE((int64_t)time(NULL) - hdr.cache_time
                     <= CONFIG.refresh_timeout);
    TEST_ASSERT_EQUAL_INT64(1000000, hdr.remote_mtime);

    /*
     * remote_mtime handling: the upstream Last-Modified changed (the
     * refreshed directory listing reports a new time for the file), so the
     * container must be invalidated and recreated.
     */
    link->time = 2000000;
    cf = Cache_open("test-item");
    TEST_ASSERT_NOT_NULL(cf);
    Cache_close(cf);

    f = fopen(full_path, "r");
    TEST_ASSERT_NOT_NULL(f);
    if (f == NULL) {
        return;
    }
    TEST_ASSERT_EQUAL_INT(CACHE_HEADER_SIZE,
                          (int)fread(&hdr, 1, CACHE_HEADER_SIZE, f));
    fclose(f);
    TEST_ASSERT_EQUAL_INT64(2000000, hdr.remote_mtime);

    // Cleanup
    ROOT_LINK_TBL = old_root_link_tbl;
    LinkTable_free(table);
    CacheSystem_cleanup();
    CONFIG.cache_dir = old_cache_dir;
    FREE(cache_key);
    cleanup_temp_dir(tmp_cache_dir);
}

void test_container_file_then_dir(void)
{
    char tmp_cache_dir[] = "./test_container_file_then_dir_dir";
    setup_temp_cache_dir(tmp_cache_dir);

    char *old_cache_dir = CONFIG.cache_dir;
    CONFIG.cache_dir = tmp_cache_dir;

    /*
     * The resource "https://example.com/test-item" was first cached as a
     * file. Its payload happens to be a valid HTML directory listing.
     */
    const char *html = "<html><body>\n"
                       "<a href=\"child.bin\">child.bin</a>\n"
                       "</body></html>";

    LinkTable *table = LinkTable_alloc("https://example.com/");
    Link *link = CALLOC(1, sizeof(Link));
    link->type = LINK_FILE;
    link->content_length = strlen(html);
    link->time = 1000000;
    link->parent_table = table;
    strncpy(link->linkname, "test-item", NAME_MAX);
    snprintf(link->f_url, PATH_MAX, "https://example.com/test-item");
    LinkTable_add(table, link);

    LinkTable *old_root_link_tbl = ROOT_LINK_TBL;
    ROOT_LINK_TBL = table;
    CacheSystem_init("https://example.com/");

    /*
     * Create the file cache and fill its payload region with the HTML
     * (simulating a completed file download).
     */
    TEST_ASSERT_EQUAL_INT(0, Cache_create("test-item"));
    Cache *cf = Cache_open("test-item");
    TEST_ASSERT_NOT_NULL(cf);
    /*
     * Fill the payload region (at cf->header_size) with the HTML,
     * simulating a completed file download.
     */
    PTHREAD_MUTEX_LOCK(&cf->w_lock);
    TEST_ASSERT_EQUAL_INT(0, fseeko(cf->fp, cf->header_size, SEEK_SET));
    TEST_ASSERT_EQUAL_INT((int)strlen(html),
                          (int)fwrite(html, 1, strlen(html), cf->fp));
    /*
     * Mark every segment as downloaded in the on-disk bitmap, simulating a
     * completed file download: only a fully downloaded payload (all segments
     * present) may be served back as a directory listing.
     */
    for (long i = 0; i < cf->segbc; i++) {
        cf->seg[i] = 1;
    }
    TEST_ASSERT_EQUAL_INT(0, fseeko(cf->fp, cf->bitmap_offset, SEEK_SET));
    TEST_ASSERT_EQUAL_INT((int)cf->segbc,
                          (int)fwrite(cf->seg, 1, (size_t)cf->segbc, cf->fp));
    fflush(cf->fp);
    PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
    Cache_close(cf);

    /*
     * Now the same URL is opened as a directory: the existing cached
     * payload is re-parsed in memory with zero byte-shifting or file
     * conversion.
     */
    char *out_payload = NULL;
    size_t out_payload_len = 0;
    char *out_header = NULL;
    size_t out_header_len = 0;
    TEST_ASSERT_EQUAL_INT(1, CacheContainer_read(link->f_url, &out_payload,
                                                 &out_payload_len, &out_header,
                                                 &out_header_len));
    TEST_ASSERT_EQUAL_INT((int)strlen(html), (int)out_payload_len);
    TEST_ASSERT_EQUAL_MEMORY(html, out_payload, strlen(html));

    LinkTable *tbl = LinkTable_alloc(link->f_url);
    LinkTable_parse_html(tbl, link->f_url, out_payload);
    TEST_ASSERT_TRUE(tbl->size > 1);
    int found_child = 0;
    for (int i = 1; i < tbl->size; i++) {
        if (!strcmp(tbl->links[i]->linkname, "child.bin")) {
            found_child = 1;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, found_child);
    LinkTable_free(tbl);
    FREE(out_payload);
    FREE(out_header);

    // Cleanup
    ROOT_LINK_TBL = old_root_link_tbl;
    LinkTable_free(table);
    CacheSystem_cleanup();
    CONFIG.cache_dir = old_cache_dir;
    cleanup_temp_dir(tmp_cache_dir);
}

void test_cache_clear_host(void)
{
    /*
     * XDG_CACHE_HOME must be an absolute path: a relative value is treated
     * as unset, so build an absolute one from the current working directory.
     */
    char *cwd = REALPATH("./", NULL);
    TEST_ASSERT_NOT_NULL(cwd);
    char *tmp_xdg_cache = path_append(cwd, "test_cache_clear_host_xdg");
    TEST_ASSERT_NOT_NULL(tmp_xdg_cache);
    FREE(cwd);
    cleanup_temp_dir(tmp_xdg_cache);
    TEST_ASSERT_EQUAL_INT(0, mkdir(tmp_xdg_cache, S_IRWXU));

    char *old_cache_dir = CONFIG.cache_dir;
    char *old_xdg = NULL;
    const char *xdg = getenv("XDG_CACHE_HOME");
    if (xdg) {
        old_xdg = STRDUP(xdg);
    }
    setenv("XDG_CACHE_HOME", tmp_xdg_cache, 1);
    CONFIG.cache_dir = NULL;

    /*
     * Build two host origin directories under the default cache root, each
     * containing a shard directory and a container file.
     */
    char example_dir[512];
    char other_dir[512];
    snprintf(example_dir, sizeof(example_dir),
             "%s/httpdirfs/https%%3A%%2F%%2Fexample.com", tmp_xdg_cache);
    snprintf(other_dir, sizeof(other_dir),
             "%s/httpdirfs/https%%3A%%2F%%2Fother.com", tmp_xdg_cache);
    char file_dir[512 + 8];
    snprintf(file_dir, sizeof(file_dir), "%s/ab", example_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir_p(file_dir, S_IRWXU));
    char example_file[512 + 64];
    snprintf(example_file, sizeof(example_file), "%s/deadbeef", file_dir);
    FILE *efile = fopen(example_file, "w");
    TEST_ASSERT_NOT_NULL(efile);
    if (efile == NULL) {
        return;
    }
    fclose(efile);
    snprintf(file_dir, sizeof(file_dir), "%s/ab", other_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir_p(file_dir, S_IRWXU));
    char other_file[512 + 64];
    snprintf(other_file, sizeof(other_file), "%s/cafebabe", file_dir);
    FILE *ofile = fopen(other_file, "w");
    TEST_ASSERT_NOT_NULL(ofile);
    if (ofile == NULL) {
        return;
    }
    fclose(ofile);

    /*
     * Bare host argument: only the target host's cache folder is removed.
     */
    TEST_ASSERT_EQUAL_INT(1, CacheSystem_delete_host("example.com"));
    struct stat st;
    TEST_ASSERT_NOT_EQUAL_INT(0, stat(example_dir, &st));
    TEST_ASSERT_EQUAL_INT(0, stat(other_dir, &st));

    /*
     * Full URL argument: same effect for that origin.
     */
    snprintf(file_dir, sizeof(file_dir), "%s/ab", example_dir);
    TEST_ASSERT_EQUAL_INT(0, mkdir_p(file_dir, S_IRWXU));
    efile = fopen(example_file, "w");
    TEST_ASSERT_NOT_NULL(efile);
    if (efile == NULL) {
        return;
    }
    fclose(efile);
    TEST_ASSERT_EQUAL_INT(
        1, CacheSystem_delete_host("https://example.com/test-item"));
    TEST_ASSERT_NOT_EQUAL_INT(0, stat(example_dir, &st));
    TEST_ASSERT_EQUAL_INT(0, stat(other_dir, &st));

    /*
     * Unknown host: nothing found, nothing deleted.
     */
    TEST_ASSERT_EQUAL_INT(0, CacheSystem_delete_host("unknown.com"));
    TEST_ASSERT_EQUAL_INT(0, stat(other_dir, &st));

    // Cleanup
    unsetenv("XDG_CACHE_HOME");
    if (old_xdg) {
        setenv("XDG_CACHE_HOME", old_xdg, 1);
        FREE(old_xdg);
    }
    CONFIG.cache_dir = old_cache_dir;
    cleanup_temp_dir(tmp_xdg_cache);
    FREE(tmp_xdg_cache);
}

void test_container_head_write_read(void)
{
    char tmp_cache_dir[] = "./test_container_head_dir";
    setup_temp_cache_dir(tmp_cache_dir);

    char *old_cache_dir = CONFIG.cache_dir;
    CONFIG.cache_dir = tmp_cache_dir;
    CacheSystem_init("https://example.com/");

    const char *url = "https://example.com/test-head.bin";
    const char *headers
        = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n\r\n";

    TEST_ASSERT_EQUAL_INT(
        0, CacheContainer_write_head(url, 200, 123456, 1700000000,
                                     "application/octet-stream", headers,
                                     strlen(headers), LINK_FILE));

    CacheStat cs;
    TEST_ASSERT_EQUAL_INT(1, CacheContainer_read_head(url, &cs));
    TEST_ASSERT_EQUAL_INT64(123456, cs.content_length);
    TEST_ASSERT_EQUAL_INT64(1700000000, cs.remote_mtime);
    TEST_ASSERT_EQUAL_INT(LINK_FILE, cs.link_type);
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", cs.content_type);

    /* Test canonicalized lookup with different casing, default port, fragment
     */
    CacheStat cs2;
    TEST_ASSERT_EQUAL_INT(
        1, CacheContainer_read_head(
               "HTTPS://EXAMPLE.COM:443/test-head.bin#frag", &cs2));
    TEST_ASSERT_EQUAL_INT64(123456, cs2.content_length);
    TEST_ASSERT_EQUAL_INT64(1700000000, cs2.remote_mtime);
    TEST_ASSERT_EQUAL_INT(LINK_FILE, cs2.link_type);

    /* Reading payload data from a HEAD-only container must return 0 (not
     * cached), and must NOT corrupt or delete the container.
     */
    char *payload = NULL;
    size_t payload_len = 0;
    char *http_hdr = NULL;
    size_t http_hdr_len = 0;
    TEST_ASSERT_EQUAL_INT(0, CacheContainer_read(url, &payload, &payload_len,
                                                 &http_hdr, &http_hdr_len));
    TEST_ASSERT_NULL(payload);
    TEST_ASSERT_NULL(http_hdr);

    /* Verify the HEAD container is still intact and readable */
    CacheStat cs3;
    TEST_ASSERT_EQUAL_INT(1, CacheContainer_read_head(url, &cs3));
    TEST_ASSERT_EQUAL_INT64(123456, cs3.content_length);

    CacheSystem_cleanup();
    CONFIG.cache_dir = old_cache_dir;
    cleanup_temp_dir(tmp_cache_dir);
}

void test_container_head_to_data_promotion(void)
{
    char tmp_cache_dir[] = "./test_container_promo_dir";
    setup_temp_cache_dir(tmp_cache_dir);

    char *old_cache_dir = CONFIG.cache_dir;
    CONFIG.cache_dir = tmp_cache_dir;
    CacheSystem_init("https://example.com/");

    LinkTable *old_root = ROOT_LINK_TBL;
    LinkTable *table = LinkTable_alloc("https://example.com/");
    ROOT_LINK_TBL = table;

    Link *link = CALLOC(1, sizeof(Link));
    strncpy(link->linkname, "promoted.bin", NAME_MAX);
    strncpy(link->f_url, "https://example.com/promoted.bin", PATH_MAX);
    link->type = LINK_FILE;
    link->content_length = (size_t)1 << 20; // 1 MB
    link->time = 1000000;
    LinkTable_add(table, link);

    /* 1. Write HEAD metadata first */
    const char *raw_hdr
        = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(
        0, CacheContainer_write_head(link->f_url, 200,
                                     (curl_off_t)link->content_length,
                                     link->time, "application/octet-stream",
                                     raw_hdr, strlen(raw_hdr), LINK_FILE));

    /* 2. Open via Cache_open: should promote HEAD container to sparse data
     * container */
    Cache *cf = Cache_open("promoted.bin");
    TEST_ASSERT_NOT_NULL(cf);
    TEST_ASSERT_EQUAL_INT(1, cf->cache_opened);
    TEST_ASSERT_NOT_NULL(cf->seg);

    /* Check that file on disk is sparse-allocated to full size */
    char *cache_key = url_to_cache_path(link->f_url);
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", CACHE_DIR, cache_key);
    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, stat(full_path, &st));
    TEST_ASSERT_EQUAL_INT64(cf->header_size + (off_t)link->content_length,
                            st.st_size);

    /* Read header to verify preserved flags and header len */
    FILE *f = fopen(full_path, "r");
    TEST_ASSERT_NOT_NULL(f);
    if (f == NULL) {
        return;
    }
    CacheHeader hdr;
    TEST_ASSERT_EQUAL_INT(CACHE_HEADER_SIZE,
                          (int)fread(&hdr, 1, CACHE_HEADER_SIZE, f));
    fclose(f);
    TEST_ASSERT_TRUE(hdr.flags & CACHE_FLAG_IS_SPARSE);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)strlen(raw_hdr), hdr.http_header_len);
    TEST_ASSERT_EQUAL_INT64(1000000, hdr.remote_mtime);

    Cache_close(cf);
    ROOT_LINK_TBL = old_root;
    LinkTable_free(table);
    CacheSystem_cleanup();
    CONFIG.cache_dir = old_cache_dir;
    FREE(cache_key);
    cleanup_temp_dir(tmp_cache_dir);
}

void test_container_redirect_pointer(void)
{
    char tmp_cache_dir[] = "./test_container_redirect_dir";
    setup_temp_cache_dir(tmp_cache_dir);

    char *old_cache_dir = CONFIG.cache_dir;
    CONFIG.cache_dir = tmp_cache_dir;
    CacheSystem_init("https://example.com/");

    const char *source_url = "https://example.com/folder";
    const char *target_url = "https://example.com/folder/";

    /* Write redirect pointer: /folder -> /folder/ */
    TEST_ASSERT_EQUAL_INT(
        0, CacheContainer_write_redirect(source_url, target_url, 301));

    /* Write target HEAD metadata under /folder/ */
    TEST_ASSERT_EQUAL_INT(0, CacheContainer_write_head(target_url, 200, 0,
                                                       1700000000, "text/html",
                                                       NULL, 0, LINK_DIR));

    /* Querying source URL transparently follows redirect pointer to target */
    CacheStat cs;
    TEST_ASSERT_EQUAL_INT(1, CacheContainer_read_head(source_url, &cs));
    TEST_ASSERT_EQUAL_INT(LINK_DIR, cs.link_type);
    TEST_ASSERT_EQUAL_INT64(1700000000, cs.remote_mtime);

    /* Also verify transparent read of directory HTML payload */
    const char *html = "<html><body>test</body></html>";
    TEST_ASSERT_EQUAL_INT(
        0, CacheContainer_write(target_url, html, strlen(html), NULL, 0));
    char *out_payload = NULL;
    size_t out_len = 0;
    char *out_hdr = NULL;
    size_t out_hdr_len = 0;
    TEST_ASSERT_EQUAL_INT(1, CacheContainer_read(source_url, &out_payload,
                                                 &out_len, &out_hdr,
                                                 &out_hdr_len));
    TEST_ASSERT_NOT_NULL(out_payload);
    TEST_ASSERT_EQUAL_STRING(html, out_payload);
    FREE(out_payload);
    FREE(out_hdr);

    /* Circular redirect loop guard test: A -> B -> A */
    const char *url_a = "https://example.com/loop_a";
    const char *url_b = "https://example.com/loop_b";
    TEST_ASSERT_EQUAL_INT(0, CacheContainer_write_redirect(url_a, url_b, 302));
    TEST_ASSERT_EQUAL_INT(0, CacheContainer_write_redirect(url_b, url_a, 302));

    CacheStat loop_cs;
    TEST_ASSERT_EQUAL_INT(0, CacheContainer_read_head(url_a, &loop_cs));

    CacheSystem_cleanup();
    CONFIG.cache_dir = old_cache_dir;
    cleanup_temp_dir(tmp_cache_dir);
}

void test_container_head_and_html_expiration(void)
{
    char tmp_cache_dir[] = "./test_container_expire_dir";
    setup_temp_cache_dir(tmp_cache_dir);

    char *old_cache_dir = CONFIG.cache_dir;
    CONFIG.cache_dir = tmp_cache_dir;
    CacheSystem_init("https://example.com/");

    const char *url = "https://example.com/expire-test.bin";
    const char *headers = "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n";

    /* Write HEAD container */
    TEST_ASSERT_EQUAL_INT(
        0, CacheContainer_write_head(url, 200, 100, 1700000000,
                                     "application/octet-stream", headers,
                                     strlen(headers), LINK_FILE));

    /* Fresh read succeeds */
    CacheStat cs;
    TEST_ASSERT_EQUAL_INT(1, CacheContainer_read_head(url, &cs));
    TEST_ASSERT_EQUAL_INT64(100, cs.content_length);

    /* Tamper with cache_time to make it expired */
    char *cache_key = string_to_cache_path(url);
    TEST_ASSERT_NOT_NULL(cache_key);
    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", CACHE_DIR, cache_key);

    FILE *f = fopen(full_path, "r+");
    TEST_ASSERT_NOT_NULL(f);
    if (f == NULL) {
        return;
    }
    CacheHeader hdr;
    TEST_ASSERT_EQUAL_INT(CACHE_HEADER_SIZE,
                          (int)fread(&hdr, 1, CACHE_HEADER_SIZE, f));
    hdr.cache_time = (int64_t)time(NULL) - CONFIG.refresh_timeout - 10;
    TEST_ASSERT_EQUAL_INT(0, fseek(f, 0, SEEK_SET));
    TEST_ASSERT_EQUAL_INT(CACHE_HEADER_SIZE,
                          (int)fwrite(&hdr, 1, CACHE_HEADER_SIZE, f));
    fclose(f);

    /* Expired HEAD container returns 0 (not cached / expired) */
    TEST_ASSERT_EQUAL_INT(0, CacheContainer_read_head(url, &cs));

    /* Test HTML directory container expiration and
     * CacheContainer_read_with_time */
    const char *dir_url = "https://example.com/expire-dir/";
    const char *html
        = "<html><body><a href=\"file.txt\">file.txt</a></body></html>";
    TEST_ASSERT_EQUAL_INT(0, CacheContainer_write(dir_url, html, strlen(html),
                                                  headers, strlen(headers)));

    char *out_payload = NULL;
    size_t out_payload_len = 0;
    char *out_headers = NULL;
    size_t out_headers_len = 0;
    time_t cache_time = 0;

    /* Fresh read succeeds and returns cache_time */
    TEST_ASSERT_EQUAL_INT(
        1, CacheContainer_read_with_time(dir_url, &out_payload,
                                         &out_payload_len, &out_headers,
                                         &out_headers_len, &cache_time, NULL));
    TEST_ASSERT_NOT_NULL(out_payload);
    TEST_ASSERT_TRUE(cache_time > 0);
    FREE(out_payload);
    FREE(out_headers);

    /* Tamper with dir container cache_time */
    char *dir_cache_key = string_to_cache_path(dir_url);
    TEST_ASSERT_NOT_NULL(dir_cache_key);
    snprintf(full_path, sizeof(full_path), "%s/%s", CACHE_DIR, dir_cache_key);

    f = fopen(full_path, "r+");
    TEST_ASSERT_NOT_NULL(f);
    if (f == NULL) {
        return;
    }
    TEST_ASSERT_EQUAL_INT(CACHE_HEADER_SIZE,
                          (int)fread(&hdr, 1, CACHE_HEADER_SIZE, f));
    hdr.cache_time = (int64_t)time(NULL) - CONFIG.refresh_timeout - 10;
    TEST_ASSERT_EQUAL_INT(0, fseek(f, 0, SEEK_SET));
    TEST_ASSERT_EQUAL_INT(CACHE_HEADER_SIZE,
                          (int)fwrite(&hdr, 1, CACHE_HEADER_SIZE, f));
    fclose(f);

    /* Expired dir container returns 0 */
    TEST_ASSERT_EQUAL_INT(
        0, CacheContainer_read_with_time(dir_url, &out_payload,
                                         &out_payload_len, &out_headers,
                                         &out_headers_len, &cache_time, NULL));

    CacheSystem_cleanup();
    CONFIG.cache_dir = old_cache_dir;
    FREE(cache_key);
    FREE(dir_cache_key);
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
    RUN_TEST(test_Cache_alloc_num_bg_workers);
    RUN_TEST(test_Cache_free_active_downloads);
    RUN_TEST(test_Cache_free_active_downloads_with_waiters);
    RUN_TEST(test_cache_path_derivation);
    RUN_TEST(test_container_file_create_open);
    RUN_TEST(test_container_head_write_read);
    RUN_TEST(test_container_head_to_data_promotion);
    RUN_TEST(test_container_redirect_pointer);
    RUN_TEST(test_container_html_parse_on_the_fly);
    RUN_TEST(test_container_timestamps);
    RUN_TEST(test_container_head_and_html_expiration);
    RUN_TEST(test_container_file_then_dir);
    RUN_TEST(test_cache_clear_host);
    return UNITY_END();
}
