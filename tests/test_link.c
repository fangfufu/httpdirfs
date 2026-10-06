/*
 * HTTPDirFS - HTTP Directory Filesystem
 *
 * Copyright (C) 2020-2026 Fufu Fang <fangfufu2003@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the OpenSSL
 * library.
 */

/**
 * \file test_link.c
 * \brief Unit tests for link.c, including external-link helper functions
 */

#include "../src/config.h"
#include "../src/link.h"
#include "../src/transfer.h"
#include "../src/util.h"

#include <stdlib.h>
#include <string.h>
#include <unity.h>

void setUp(void)
{
    Config_init();
}

void tearDown(void)
{
    CONFIG.allow_external_origin = 0;
    CONFIG.website_mode = 0;
    CONFIG.ignore_anchors = 0;
    if (ROOT_LINK_TBL != NULL) {
        LinkTable_free(ROOT_LINK_TBL);
        ROOT_LINK_TBL = NULL;
    }
}

/* ========================================================================= */
/* is_external_url() tests                                                   */
/* ========================================================================= */

void test_is_external_url_http(void)
{
    TEST_ASSERT_EQUAL_INT(1, is_external_url("http://example.com/file.iso"));
}

void test_is_external_url_https(void)
{
    TEST_ASSERT_EQUAL_INT(1, is_external_url("https://example.com/file.iso"));
}

void test_is_external_url_relative(void)
{
    TEST_ASSERT_EQUAL_INT(0, is_external_url("file.iso"));
}

void test_is_external_url_absolute_path(void)
{
    TEST_ASSERT_EQUAL_INT(0, is_external_url("/path/file.iso"));
}

void test_is_external_url_ftp(void)
{
    /* ftp:// is NOT treated as an external http/https URL */
    TEST_ASSERT_EQUAL_INT(0, is_external_url("ftp://example.com/file.iso"));
}

void test_is_external_url_null(void)
{
    TEST_ASSERT_EQUAL_INT(0, is_external_url(NULL));
}

void test_is_external_url_uppercase(void)
{
    TEST_ASSERT_EQUAL_INT(1, is_external_url("HTTP://example.com/file.iso"));
    TEST_ASSERT_EQUAL_INT(1, is_external_url("HTTPS://example.com/file.iso"));
}

/* ========================================================================= */
/* is_cross_origin() tests                                                   */
/* ========================================================================= */

void test_is_cross_origin_different_host(void)
{
    TEST_ASSERT_EQUAL_INT(
        1, is_cross_origin("http://localhost/", "http://example.com/f.iso"));
}

void test_is_cross_origin_same_host(void)
{
    TEST_ASSERT_EQUAL_INT(
        0, is_cross_origin("http://localhost/", "http://localhost/file.iso"));
}

void test_is_cross_origin_different_scheme(void)
{
    TEST_ASSERT_EQUAL_INT(
        1, is_cross_origin("http://example.com/", "https://example.com/f.iso"));
}

void test_is_cross_origin_different_port(void)
{
    TEST_ASSERT_EQUAL_INT(1, is_cross_origin("http://localhost:8080/",
                                             "http://localhost:9090/f.iso"));
}

void test_is_cross_origin_same_host_with_port(void)
{
    TEST_ASSERT_EQUAL_INT(0, is_cross_origin("http://localhost:8080/",
                                             "http://localhost:8080/f.iso"));
}

void test_is_cross_origin_no_trailing_slash_same(void)
{
    TEST_ASSERT_EQUAL_INT(
        0, is_cross_origin("http://example.com", "http://example.com/f.iso"));
}

void test_is_cross_origin_no_trailing_slash_different(void)
{
    TEST_ASSERT_EQUAL_INT(
        1, is_cross_origin("http://example.com", "http://other.com/f.iso"));
}

void test_is_cross_origin_both_no_trailing_slash_same(void)
{
    TEST_ASSERT_EQUAL_INT(
        0, is_cross_origin("http://example.com", "http://example.com"));
}

void test_is_cross_origin_null(void)
{
    TEST_ASSERT_EQUAL_INT(1, is_cross_origin(NULL, "http://example.com/"));
    TEST_ASSERT_EQUAL_INT(1, is_cross_origin("http://example.com/", NULL));
    TEST_ASSERT_EQUAL_INT(1, is_cross_origin(NULL, NULL));
}

void test_is_cross_origin_case_insensitive(void)
{
    TEST_ASSERT_EQUAL_INT(
        0, is_cross_origin("http://LOCALHOST/", "http://localhost/file.iso"));
    TEST_ASSERT_EQUAL_INT(
        0, is_cross_origin("HTTP://localhost/", "http://localhost/file.iso"));
}

void test_is_cross_origin_with_query_and_fragment(void)
{
    /* Test URLs with query params/fragments containing slashes */
    TEST_ASSERT_EQUAL_INT(
        0, is_cross_origin("http://localhost/",
                           "http://localhost?redirect=/foo/bar"));
    TEST_ASSERT_EQUAL_INT(0,
                          is_cross_origin("http://localhost?redirect=/foo/bar",
                                          "http://localhost/file.iso"));

    /* Test URLs with query params/fragments but fewer than 3 slashes */
    TEST_ASSERT_EQUAL_INT(0, is_cross_origin("http://localhost?auth=1",
                                             "http://localhost/file.iso"));
    TEST_ASSERT_EQUAL_INT(0, is_cross_origin("http://localhost#fragment",
                                             "http://localhost?auth=1"));
    TEST_ASSERT_EQUAL_INT(1, is_cross_origin("http://localhost?auth=1",
                                             "http://otherhost?auth=1"));
}

void test_is_cross_origin_default_port_normalization_http(void)
{
    TEST_ASSERT_EQUAL_INT(0, is_cross_origin("http://example.com/",
                                             "http://example.com:80/f.iso"));
    TEST_ASSERT_EQUAL_INT(0, is_cross_origin("http://example.com:80/",
                                             "http://example.com/f.iso"));
}

void test_is_cross_origin_default_port_normalization_https(void)
{
    TEST_ASSERT_EQUAL_INT(0, is_cross_origin("https://example.com/",
                                             "https://example.com:443/f.iso"));
    TEST_ASSERT_EQUAL_INT(0, is_cross_origin("https://example.com:443/",
                                             "https://example.com/f.iso"));
}

void test_is_cross_origin_non_default_port_not_equal(void)
{
    TEST_ASSERT_EQUAL_INT(1, is_cross_origin("http://example.com/",
                                             "http://example.com:8080/f.iso"));
    TEST_ASSERT_EQUAL_INT(1, is_cross_origin("https://example.com/",
                                             "https://example.com:8443/f.iso"));
}


/* ========================================================================= */
/* HTML_to_LinkTable() integration tests via LinkTable_parse_html()          */
/* ========================================================================= */

void test_HTML_external_link_file(void)
{
    CONFIG.allow_external_origin = 1;
    LinkTable *tbl = LinkTable_alloc("http://localhost/");
    LinkTable_parse_html(tbl, "http://localhost/",
                         "<a href=\"http://external.com/file.iso\">"
                         "file.iso</a>");

    /* Expect 2 entries: head link + one external file link */
    TEST_ASSERT_EQUAL_INT(2, tbl->size);
    TEST_ASSERT_EQUAL_STRING("file.iso", tbl->links[1]->linkname);
    TEST_ASSERT_EQUAL_STRING("http://external.com/file.iso",
                             tbl->links[1]->f_url);
    TEST_ASSERT_EQUAL_INT(LINK_UNINITIALISED_FILE, tbl->links[1]->type);
    LinkTable_free(tbl);
    CONFIG.allow_external_origin = 0;
}

void test_HTML_external_link_dir(void)
{
    CONFIG.allow_external_origin = 1;
    LinkTable *tbl = LinkTable_alloc("http://localhost/");
    LinkTable_parse_html(tbl, "http://localhost/",
                         "<a href=\"http://external.com/subdir/\">"
                         "subdir</a>"
                         "<a href=\"http://external.com/subdir2/?q=1\">"
                         "subdir2</a>"
                         "<a href=\"http://external.com/subdir3/#fragment\">"
                         "subdir3</a>");

    TEST_ASSERT_EQUAL_INT(4, tbl->size);
    TEST_ASSERT_EQUAL_STRING("subdir", tbl->links[1]->linkname);
    TEST_ASSERT_EQUAL_INT(LINK_UNINITIALISED_DIR, tbl->links[1]->type);

    TEST_ASSERT_EQUAL_STRING("subdir2", tbl->links[2]->linkname);
    TEST_ASSERT_EQUAL_INT(LINK_UNINITIALISED_DIR, tbl->links[2]->type);

    TEST_ASSERT_EQUAL_STRING("subdir3", tbl->links[3]->linkname);
    TEST_ASSERT_EQUAL_INT(LINK_UNINITIALISED_DIR, tbl->links[3]->type);

    LinkTable_free(tbl);
    CONFIG.allow_external_origin = 0;
}

void test_HTML_external_link_disabled(void)
{
    CONFIG.allow_external_origin = 0; /* flag is OFF by default */
    LinkTable *tbl = LinkTable_alloc("http://localhost/");
    LinkTable_parse_html(tbl, "http://localhost/",
                         "<a href=\"http://external.com/file.iso\">"
                         "file.iso</a>");

    /* Only the head link should be present */
    TEST_ASSERT_EQUAL_INT(1, tbl->size);
    LinkTable_free(tbl);
}

void test_HTML_external_link_same_origin(void)
{
    /* Full absolute URL pointing to SAME origin is accepted */
    CONFIG.allow_external_origin = 0;
    LinkTable *tbl = LinkTable_alloc("http://localhost/");
    LinkTable_parse_html(tbl, "http://localhost/",
                         "<a href=\"http://localhost/file.iso\">"
                         "file.iso</a>");

    TEST_ASSERT_EQUAL_INT(2, tbl->size);
    TEST_ASSERT_EQUAL_STRING("file.iso", tbl->links[1]->linkname);
    TEST_ASSERT_EQUAL_STRING("http://localhost/file.iso", tbl->links[1]->f_url);
    LinkTable_free(tbl);
}

void test_HTML_duplicate_target_url_first_wins(void)
{
    CONFIG.allow_external_origin = 1;
    LinkTable *tbl = LinkTable_alloc("http://localhost/");
    LinkTable_parse_html(tbl, "http://localhost/",
                         "<a href=\"http://server-a.com/file.iso\">First Anchor"
                         "</a><a href=\"http://server-a.com/file.iso\">"
                         "Second Anchor</a>");

    /* Only one link: duplicate target URL is dropped, first anchor text wins */
    TEST_ASSERT_EQUAL_INT(2, tbl->size);
    TEST_ASSERT_EQUAL_STRING("First Anchor-file.iso", tbl->links[1]->linkname);
    TEST_ASSERT_EQUAL_STRING("http://server-a.com/file.iso",
                             tbl->links[1]->f_url);
    LinkTable_free(tbl);
    CONFIG.allow_external_origin = 0;
}

void test_HTML_external_link_dot_and_dotdot(void)
{
    CONFIG.allow_external_origin = 1;
    LinkTable *tbl = LinkTable_alloc("http://localhost/");
    LinkTable_parse_html(
        tbl, "http://localhost/",
        "<a href=\"http://external.com/.\">.</a>"
        "<a href=\"http://external.com/..\">..</a>"
        "<a href=\"http://external.com/file.iso\">file.iso</a>");

    /* Expect 2 entries: HEAD link + file.iso (ignoring . and ..) */
    TEST_ASSERT_EQUAL_INT(2, tbl->size);
    TEST_ASSERT_EQUAL_STRING("file.iso", tbl->links[1]->linkname);
    LinkTable_free(tbl);
    CONFIG.allow_external_origin = 0;
}

/* ========================================================================= */
/* LinkTable_fill() skip test                                                */
/* ========================================================================= */

void test_Link_preserves_preset_f_url(void)
{
    /* Verify the invariant: a link with f_url pre-set by HTML_to_LinkTable
     * retains its f_url.  We check this directly on the link struct rather
     * than calling LinkTable_fill() (which would make real HTTP requests). */
    LinkTable *tbl = LinkTable_alloc("http://localhost/");

    Link *ext = CALLOC(1, sizeof(Link));
    strncpy(ext->linkname, "file.iso", NAME_MAX);
    strncpy(ext->linkpath, "file.iso", NAME_MAX);
    strncpy(ext->f_url, "http://external.com/file.iso", PATH_MAX);
    ext->type = LINK_UNINITIALISED_FILE;
    LinkTable_add(tbl, ext);

    TEST_ASSERT_EQUAL_STRING("http://external.com/file.iso", ext->f_url);
    LinkTable_free(tbl);
}

/* ========================================================================= */
/* url_to_cache_path() tests                                                 */
/* ========================================================================= */

void test_url_to_cache_path_null(void)
{
    TEST_ASSERT_NULL(url_to_cache_path(NULL));
}

static void check_hash_path_format(const char *url, const char *path)
{
    TEST_ASSERT_NOT_NULL(path);
    /*
     * Expected format: "<first 2 hex of md5>/<full 32-char md5>"
     */
    TEST_ASSERT_EQUAL_INT(35, (int)strlen(path));
    TEST_ASSERT_EQUAL_CHAR('/', path[2]);
    char *hash = generate_md5sum(url);
    TEST_ASSERT_NOT_NULL(hash);
    TEST_ASSERT_EQUAL_INT(0, strncmp(path, hash, 2));
    TEST_ASSERT_EQUAL_INT(0, strncmp(path + 3, hash, 32));
    FREE(hash);
}

void test_url_to_cache_path_local(void)
{
    char *path = url_to_cache_path("http://localhost/my%20file.iso");
    check_hash_path_format("http://localhost/my%20file.iso", path);
    /*
     * Deterministic: the same URL always derives the same cache path.
     */
    char *path_again = url_to_cache_path("http://localhost/my%20file.iso");
    TEST_ASSERT_EQUAL_STRING(path, path_again);
    FREE(path);
    FREE(path_again);
}

void test_url_to_cache_path_external_sanitization(void)
{
    /*
     * Cross-origin URLs are keyed by the raw URL hash, exactly like
     * same-origin ones.
     */
    char *path = url_to_cache_path("http://external.com/my%20file.iso?param=1");
    check_hash_path_format("http://external.com/my%20file.iso?param=1", path);
    FREE(path);
}

void test_url_to_cache_path_same_origin_different_path(void)
{
    const char *urls[4]
        = {"https://example.com/view/1001/file.iso",
           "https://example.com/browse/1001", "https://example.com/",
           "https://example.com/other-folder"};
    char *paths[4] = {0};

    for (int i = 0; i < 4; i++) {
        paths[i] = url_to_cache_path(urls[i]);
        check_hash_path_format(urls[i], paths[i]);
    }

    /*
     * Different URLs must derive different cache paths (no
     * file-versus-directory collisions, no truncation).
     */
    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 4; j++) {
            TEST_ASSERT_NOT_EQUAL(0, strcmp(paths[i], paths[j]));
        }
    }

    for (int i = 0; i < 4; i++) {
        FREE(paths[i]);
    }
}

void test_url_to_cache_path_same_origin_encoding_divergence(void)
{
    /*
     * The cache key is the MD5 of the canonical URL string, so different
     * encodings of the same resource derive different (valid) paths.
     */
    char *path1 = url_to_cache_path("http://localhost/my folder/file.txt");
    check_hash_path_format("http://localhost/my folder/file.txt", path1);

    char *path2 = url_to_cache_path("http://localhost/my%20folder/file.txt");
    check_hash_path_format("http://localhost/my%20folder/file.txt", path2);

    TEST_ASSERT_NOT_EQUAL(0, strcmp(path1, path2));
    FREE(path1);
    FREE(path2);
}

void test_get_server_root(void)
{
    TEST_ASSERT_NULL(get_server_root(NULL));

    char *r1 = get_server_root("https://subdomain.example.com/browse/1001/"
                               "archive.iso");
    TEST_ASSERT_NOT_NULL(r1);
    TEST_ASSERT_EQUAL_STRING("https://subdomain.example.com", r1);
    FREE(r1);

    char *r2 = get_server_root("http://localhost:34521/folder/file.bin");
    TEST_ASSERT_NOT_NULL(r2);
    TEST_ASSERT_EQUAL_STRING("http://localhost:34521", r2);
    FREE(r2);

    char *r3 = get_server_root("http://example.com:80/file.bin");
    TEST_ASSERT_NOT_NULL(r3);
    TEST_ASSERT_EQUAL_STRING("http://example.com", r3);
    FREE(r3);

    char *r4 = get_server_root("https://example.com:443/");
    TEST_ASSERT_NOT_NULL(r4);
    TEST_ASSERT_EQUAL_STRING("https://example.com", r4);
    FREE(r4);

    char *r5 = get_server_root("https://example.com");
    TEST_ASSERT_NOT_NULL(r5);
    TEST_ASSERT_EQUAL_STRING("https://example.com", r5);
    FREE(r5);
}

void test_url_to_cache_path_long_url_hashed(void)
{
    char long_url[300];
    memset(long_url, 'a', sizeof(long_url));
    memcpy(long_url, "http://external.com/", 20);
    long_url[299] = '\0';

    /*
     * Long URLs are immune to the NAME_MAX / PATH_MAX limits: the cache
     * path is always the fixed-size "ab/<hash>" string.
     */
    char *path = url_to_cache_path(long_url);
    check_hash_path_format(long_url, path);
    FREE(path);
}

void test_canonicalize_url(void)
{
    TEST_ASSERT_NULL(canonicalize_url(NULL));

    char *c1 = canonicalize_url("HTTP://EXAMPLE.COM/foo");
    TEST_ASSERT_NOT_NULL(c1);
    TEST_ASSERT_EQUAL_STRING("http://example.com/foo", c1);
    FREE(c1);

    char *c2 = canonicalize_url("http://example.com:80/foo");
    TEST_ASSERT_NOT_NULL(c2);
    TEST_ASSERT_EQUAL_STRING("http://example.com/foo", c2);
    FREE(c2);

    char *c3 = canonicalize_url("https://example.com:443/foo");
    TEST_ASSERT_NOT_NULL(c3);
    TEST_ASSERT_EQUAL_STRING("https://example.com/foo", c3);
    FREE(c3);

    char *c4 = canonicalize_url("http://example.com:8080/foo");
    TEST_ASSERT_NOT_NULL(c4);
    TEST_ASSERT_EQUAL_STRING("http://example.com:8080/foo", c4);
    FREE(c4);

    char *c5 = canonicalize_url("http://example.com/foo#section1");
    TEST_ASSERT_NOT_NULL(c5);
    TEST_ASSERT_EQUAL_STRING("http://example.com/foo", c5);
    FREE(c5);

    char *c6 = canonicalize_url("http://example.com/a/b/../c");
    TEST_ASSERT_NOT_NULL(c6);
    TEST_ASSERT_EQUAL_STRING("http://example.com/a/c", c6);
    FREE(c6);

    char *c7 = canonicalize_url("http://example.com/dir//sub/file");
    TEST_ASSERT_NOT_NULL(c7);
    TEST_ASSERT_EQUAL_STRING("http://example.com/dir/sub/file", c7);
    FREE(c7);

    char *c8 = canonicalize_url("http://example.com/dir/");
    TEST_ASSERT_NOT_NULL(c8);
    TEST_ASSERT_EQUAL_STRING("http://example.com/dir/", c8);
    FREE(c8);

    /* Verify that uncanonicalized variations produce identical cache paths */
    char *p1 = url_to_cache_path("HTTP://Example.COM:80/dir/../file#section");
    char *p2 = url_to_cache_path("http://example.com/file");
    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_NOT_NULL(p2);
    TEST_ASSERT_EQUAL_STRING(p1, p2);
    FREE(p1);
    FREE(p2);
}

/* ========================================================================= */
/* Pre-existing tests                                                        */
/* ========================================================================= */

void test_LinkTable_alloc(void)
{
    LinkTable *table = LinkTable_alloc("https://example.com/dir/");
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_EQUAL_INT(1, table->size);
    TEST_ASSERT_NOT_NULL(table->links);
    TEST_ASSERT_NOT_NULL(table->links[0]);
    TEST_ASSERT_EQUAL_STRING("https://example.com/dir/",
                             table->links[0]->f_url);
    TEST_ASSERT_EQUAL_STRING("", table->links[0]->linkname);
    TEST_ASSERT_TRUE(table->index_time > 0);
    LinkTable_free(table);
}

void test_LinkTable_add(void)
{
    LinkTable *table = LinkTable_alloc("https://example.com/dir/");
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_EQUAL_INT(1, table->size);

    Link *new_link = CALLOC(1, sizeof(Link));
    TEST_ASSERT_NOT_NULL(new_link);
    strncpy(new_link->linkname, "file.txt", NAME_MAX);
    new_link->type = LINK_FILE;

    LinkTable_add(table, new_link);

    TEST_ASSERT_EQUAL_INT(2, table->size);
    TEST_ASSERT_EQUAL_PTR(new_link, table->links[1]);

    LinkTable_free(table);
}

void test_Link_download_zero_length(void)
{
    Link link = {0};
    link.content_length = 0;
    link.type = LINK_FILE;
    strncpy(link.linkname, "empty.txt", NAME_MAX);
    strncpy(link.f_url, "https://example.com/empty.txt", PATH_MAX);

    char buf[10];
    memset(buf, 0xff, sizeof(buf));
    long res = Link_download(&link, buf, sizeof(buf), 0, NULL);
    TEST_ASSERT_EQUAL_INT(0, res);

    for (size_t i = 0; i < sizeof(buf); i++) {
        TEST_ASSERT_EQUAL_HEX8(0xff, (unsigned char)buf[i]);
    }
}

void test_link_linknames_equal(void)
{
    TEST_ASSERT_TRUE(link_linknames_equal("file.txt", "file.txt"));
    TEST_ASSERT_TRUE(link_linknames_equal("file.txt", "file.txt/"));
    TEST_ASSERT_TRUE(link_linknames_equal("file.txt/", "file.txt"));
    TEST_ASSERT_TRUE(link_linknames_equal("dir/", "dir/"));
    TEST_ASSERT_TRUE(link_linknames_equal("dir", "dir/"));

    TEST_ASSERT_FALSE(link_linknames_equal("file.txt", "other.txt"));
    TEST_ASSERT_FALSE(link_linknames_equal("file.txt", "file.txt//"));
    TEST_ASSERT_FALSE(link_linknames_equal("", "a/"));
    TEST_ASSERT_FALSE(link_linknames_equal("a", "b"));
}

void test_link_hash_str(void)
{
    unsigned int h1 = link_hash_str("file.txt");
    unsigned int h2 = link_hash_str("file.txt/");
    unsigned int h3 = link_hash_str("file.txt//");
    TEST_ASSERT_EQUAL_UINT(h1, h2);
    TEST_ASSERT_EQUAL_UINT(h1, h3);

    unsigned int h4 = link_hash_str("dir");
    unsigned int h5 = link_hash_str("dir/");
    TEST_ASSERT_EQUAL_UINT(h4, h5);

    unsigned int h6 = link_hash_str("other");
    TEST_ASSERT_NOT_EQUAL(h1, h6);
}

void test_LinkHashSet(void)
{
    LinkHashSet *set = LinkHashSet_new(4);
    TEST_ASSERT_NOT_NULL(set);

    TEST_ASSERT_EQUAL_INT(1, LinkHashSet_add(set, "file.txt"));
    TEST_ASSERT_EQUAL_INT(0, LinkHashSet_add(set, "file.txt"));
    TEST_ASSERT_EQUAL_INT(0, LinkHashSet_add(set, "file.txt/"));
    TEST_ASSERT_EQUAL_INT(1, LinkHashSet_add(set, "file.txt//"));

    TEST_ASSERT_EQUAL_INT(1, LinkHashSet_add(set, "other.txt"));
    TEST_ASSERT_EQUAL_INT(1, LinkHashSet_add(set, "another.txt"));

    // Trigger a resize by adding more items
    TEST_ASSERT_EQUAL_INT(1, LinkHashSet_add(set, "resize1.txt"));
    TEST_ASSERT_EQUAL_INT(1, LinkHashSet_add(set, "resize2.txt"));
    TEST_ASSERT_EQUAL_INT(0, LinkHashSet_add(set, "resize1.txt"));

    LinkHashSet_free(set);
}

void test_LinkTable_parse_html_duplicates(void)
{
    LinkTable *table = LinkTable_alloc("https://example.com/dir/");
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_EQUAL_INT(1, table->size); // Just the head link

    const char *html = "<html>\n"
                       "<body>\n"
                       "  <a href=\"a.txt\">a.txt</a>\n"
                       "  <a href=\"a.txt\">a.txt duplicate</a>\n"
                       "  <a href=\"a.txt/\">a.txt slash duplicate</a>\n"
                       "  <a href=\"b.txt\">b.txt</a>\n"
                       "  <a href=\"b.txt/\">b.txt slash duplicate</a>\n"
                       "</body>\n"
                       "</html>\n";

    LinkTable_parse_html(table, "https://example.com/dir/", html);

    // Should have size 3: HEAD link, a.txt, and b.txt
    TEST_ASSERT_EQUAL_INT(3, table->size);

    TEST_ASSERT_EQUAL_STRING("", table->links[0]->linkname);
    TEST_ASSERT_EQUAL_STRING("a.txt", table->links[1]->linkname);
    TEST_ASSERT_EQUAL_STRING("b.txt", table->links[2]->linkname);

    LinkTable_free(table);
}


void test_ignore_anchors_default(void)
{
    CONFIG.ignore_anchors = 0;
    LinkTable *table = LinkTable_alloc("https://example.com/dir/");
    TEST_ASSERT_NOT_NULL(table);

    const char *html = "<html><body>\n"
                       "  <a href=\"#directory\">Directory</a>\n"
                       "  <a href=\"file.txt\">file.txt</a>\n"
                       "</body></html>\n";

    LinkTable_parse_html(table, "https://example.com/dir/", html);

    // Intra-page fragment (#directory) is ignored; only head link and file.txt
    // exist
    TEST_ASSERT_EQUAL_INT(2, table->size);
    TEST_ASSERT_EQUAL_STRING("file.txt", table->links[1]->linkname);

    LinkTable_free(table);
}

void test_ignore_anchors_enabled(void)
{
    CONFIG.ignore_anchors = 1;
    LinkTable *table = LinkTable_alloc("https://example.com/dir/");
    TEST_ASSERT_NOT_NULL(table);

    const char *html = "<html><body>\n"
                       "  <a href=\"#directory\">Directory</a>\n"
                       "  <a href=\"file.txt\">file.txt</a>\n"
                       "</body></html>\n";

    LinkTable_parse_html(table, "https://example.com/dir/", html);

    // Expect head link and file.txt only (#directory ignored)
    TEST_ASSERT_EQUAL_INT(2, table->size);
    TEST_ASSERT_EQUAL_STRING("file.txt", table->links[1]->linkname);

    LinkTable_free(table);
}

void test_diagnostics_add(void)
{
    LinkTable *tbl = LinkTable_alloc("http://localhost/");
    TEST_ASSERT_NOT_NULL(tbl);

    const char *html = "<html>test</html>";
    const char *header = "HTTP/1.1 200 OK\r\nServer: test\r\n\r\n";
    LinkTable_add_diagnostics(tbl, html, strlen(html), header, strlen(header));

    /* tbl should now contain head link + .httpdirfs */
    TEST_ASSERT_EQUAL_INT(2, tbl->size);
    Link *diag = tbl->links[1];
    TEST_ASSERT_EQUAL_STRING(".httpdirfs", diag->linkname);
    TEST_ASSERT_EQUAL_INT(LINK_DIR, diag->type);
    TEST_ASSERT_EQUAL_INT(1, diag->is_virtual);
    TEST_ASSERT_NOT_NULL(diag->next_table);

    /* Inside .httpdirfs: head link + CONTENT + HEADER */
    const LinkTable *dtbl = diag->next_table;
    TEST_ASSERT_EQUAL_INT(3, dtbl->size);

    Link *content = dtbl->links[1];
    TEST_ASSERT_EQUAL_STRING("CONTENT", content->linkname);
    TEST_ASSERT_EQUAL_INT(LINK_FILE, content->type);
    TEST_ASSERT_EQUAL_INT(1, content->is_virtual);
    TEST_ASSERT_EQUAL_INT((int)strlen(html), (int)content->content_length);
    TEST_ASSERT_NOT_NULL(content->virtual_content);
    TEST_ASSERT_EQUAL_STRING(html, content->virtual_content);

    Link *hdr = dtbl->links[2];
    TEST_ASSERT_EQUAL_STRING("HEADER", hdr->linkname);
    TEST_ASSERT_EQUAL_INT(LINK_FILE, hdr->type);
    TEST_ASSERT_EQUAL_INT(1, hdr->is_virtual);
    TEST_ASSERT_EQUAL_INT((int)strlen(header), (int)hdr->content_length);
    TEST_ASSERT_NOT_NULL(hdr->virtual_content);
    TEST_ASSERT_EQUAL_STRING(header, hdr->virtual_content);

    LinkTable_free(tbl);
}

void test_diagnostics_path_lookup_and_read(void)
{
    ROOT_LINK_TBL = LinkTable_alloc("http://localhost/");
    TEST_ASSERT_NOT_NULL(ROOT_LINK_TBL);

    const char *html = "<html>body data</html>";
    const char *header = "HTTP/2 200\r\n";
    LinkTable_add_diagnostics(ROOT_LINK_TBL, html, strlen(html), header,
                              strlen(header));

    /* Lookup /.httpdirfs */
    Link *diag_dir = path_to_Link("/.httpdirfs");
    TEST_ASSERT_NOT_NULL(diag_dir);
    TEST_ASSERT_EQUAL_STRING(".httpdirfs", diag_dir->linkname);
    TEST_ASSERT_EQUAL_INT(LINK_DIR, diag_dir->type);
    LinkTable_unref(diag_dir->parent_table);

    /* Lookup /.httpdirfs/CONTENT */
    Link *c_link = path_to_Link("/.httpdirfs/CONTENT");
    TEST_ASSERT_NOT_NULL(c_link);
    TEST_ASSERT_EQUAL_STRING("CONTENT", c_link->linkname);
    TEST_ASSERT_EQUAL_INT(LINK_FILE, c_link->type);
    TEST_ASSERT_EQUAL_INT((int)strlen(html), (int)c_link->content_length);
    LinkTable_unref(c_link->parent_table);

    /* Lookup /.httpdirfs/HEADER */
    Link *h_link = path_to_Link("/.httpdirfs/HEADER");
    TEST_ASSERT_NOT_NULL(h_link);
    TEST_ASSERT_EQUAL_STRING("HEADER", h_link->linkname);
    TEST_ASSERT_EQUAL_INT(LINK_FILE, h_link->type);
    TEST_ASSERT_EQUAL_INT((int)strlen(header), (int)h_link->content_length);
    LinkTable_unref(h_link->parent_table);

    /* Read CONTENT from offset 0 */
    char buf[64] = {0};
    long n = path_download("/.httpdirfs/CONTENT", buf, sizeof(buf), 0);
    TEST_ASSERT_EQUAL_INT((int)strlen(html), (int)n);
    TEST_ASSERT_EQUAL_STRING(html, buf);

    /* Read CONTENT partial slice (offset 6, 4 bytes -> "body") */
    memset(buf, 0, sizeof(buf));
    n = path_download("/.httpdirfs/CONTENT", buf, 4, 6);
    TEST_ASSERT_EQUAL_INT(4, (int)n);
    TEST_ASSERT_EQUAL_STRING_LEN("body", buf, 4);

    /* Read HEADER */
    memset(buf, 0, sizeof(buf));
    n = path_download("/.httpdirfs/HEADER", buf, sizeof(buf), 0);
    TEST_ASSERT_EQUAL_INT((int)strlen(header), (int)n);
    TEST_ASSERT_EQUAL_STRING(header, buf);

    /* Read past EOF */
    n = path_download("/.httpdirfs/CONTENT", buf, sizeof(buf), 100);
    TEST_ASSERT_EQUAL_INT(0, (int)n);

    LinkTable_free(ROOT_LINK_TBL);
    ROOT_LINK_TBL = NULL;
}

void test_diagnostics_subdirectory(void)
{
    ROOT_LINK_TBL = LinkTable_alloc("http://localhost/");
    TEST_ASSERT_NOT_NULL(ROOT_LINK_TBL);

    Link *subdir = CALLOC(1, sizeof(Link));
    strncpy(subdir->linkname, "sub", NAME_MAX);
    subdir->type = LINK_DIR;
    LinkTable *sub_tbl = LinkTable_alloc("http://localhost/sub/");
    const char *html = "<html>sub content</html>";
    const char *header = "HTTP/1.1 200 OK";
    LinkTable_add_diagnostics(sub_tbl, html, strlen(html), header,
                              strlen(header));
    subdir->next_table = sub_tbl;
    sub_tbl->parent_tbl = ROOT_LINK_TBL;
    sub_tbl->parent_link = subdir;
    LinkTable_add(ROOT_LINK_TBL, subdir);

    Link *link = path_to_Link("/sub/.httpdirfs/CONTENT");
    TEST_ASSERT_NOT_NULL(link);
    TEST_ASSERT_EQUAL_INT((int)strlen(html), (int)link->content_length);
    LinkTable_unref(link->parent_table);

    char buf[64] = {0};
    long n = path_download("/sub/.httpdirfs/CONTENT", buf, sizeof(buf), 0);
    TEST_ASSERT_EQUAL_INT((int)strlen(html), (int)n);
    TEST_ASSERT_EQUAL_STRING(html, buf);

    LinkTable_free(ROOT_LINK_TBL);
    ROOT_LINK_TBL = NULL;
}

void test_diagnostics_empty(void)
{
    LinkTable *tbl = LinkTable_alloc("http://localhost/");
    TEST_ASSERT_NOT_NULL(tbl);

    LinkTable_add_diagnostics(tbl, NULL, 0, NULL, 0);
    TEST_ASSERT_EQUAL_INT(2, tbl->size);
    const LinkTable *dtbl = tbl->links[1]->next_table;
    TEST_ASSERT_EQUAL_INT(3, dtbl->size);
    TEST_ASSERT_EQUAL_INT(0, (int)dtbl->links[1]->content_length);
    TEST_ASSERT_NULL(dtbl->links[1]->virtual_content);
    TEST_ASSERT_EQUAL_INT(0, (int)dtbl->links[2]->content_length);
    TEST_ASSERT_NULL(dtbl->links[2]->virtual_content);

    LinkTable_free(tbl);
}

void test_LinkTable_expired_subdirectory(void)
{
    CONFIG.refresh_timeout = 10;
    ROOT_LINK_TBL = LinkTable_alloc("http://localhost/");
    TEST_ASSERT_NOT_NULL(ROOT_LINK_TBL);

    Link *subdir = CALLOC(1, sizeof(Link));
    strncpy(subdir->linkname, "sub", NAME_MAX);
    subdir->type = LINK_DIR;
    LinkTable *sub_tbl = LinkTable_alloc("http://localhost/sub/");
    subdir->next_table = sub_tbl;
    sub_tbl->parent_tbl = ROOT_LINK_TBL;
    sub_tbl->parent_link = subdir;
    ROOT_LINK_TBL->refcount++;
    LinkTable_add(ROOT_LINK_TBL, subdir);

    /* Set sub_tbl to be expired (older than CONFIG.refresh_timeout) */
    sub_tbl->index_time = time(NULL) - 20;

    /*
     * When traversing into /sub/file, the expired table is retired/detached
     * and a fresh table is reloaded. With no server available the reload
     * fails; a failed listing must not be attached as a fresh empty table
     * (that would hide the error until refresh_timeout expires). The link
     * therefore keeps a NULL next_table and the download is retried on the
     * next access; /sub/file is not found.
     */
    Link *link = path_to_Link("/sub/file");
    TEST_ASSERT_NULL(link);
    TEST_ASSERT_NULL(subdir->next_table);

    LinkTable_free(ROOT_LINK_TBL);
    ROOT_LINK_TBL = NULL;
}

/*
 * Phase 2: Advanced Parsing Mode Unit Tests
 */

static GumboNode *find_anchor_node(GumboNode *node)
{
    if (!node) {
        return NULL;
    }
    if (node->type == GUMBO_NODE_ELEMENT
        && node->v.element.tag == GUMBO_TAG_A) {
        return node;
    }
    if (node->type == GUMBO_NODE_ELEMENT) {
        GumboVector *children = &node->v.element.children;
        for (size_t i = 0; i < children->length; ++i) {
            GumboNode *res = find_anchor_node((GumboNode *)children->data[i]);
            if (res) {
                return res;
            }
        }
    }
    return NULL;
}

void test_resolve_target_url(void)
{
    char out[1024];

    // 1. Absolute URLs
    TEST_ASSERT_EQUAL_INT(1, resolve_target_url("https://example.com/dir/",
                                                "http://other.org/file.iso",
                                                out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("http://other.org/file.iso", out);

    TEST_ASSERT_EQUAL_INT(1, resolve_target_url("https://example.com/dir/",
                                                "https://other.org/file.iso",
                                                out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://other.org/file.iso", out);

    // 2. Scheme-relative URLs
    TEST_ASSERT_EQUAL_INT(1, resolve_target_url("https://example.com/dir/",
                                                "//other.org/file.iso", out,
                                                sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://other.org/file.iso", out);

    TEST_ASSERT_EQUAL_INT(1, resolve_target_url("http://example.com/dir/",
                                                "//other.org/file.iso", out,
                                                sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("http://other.org/file.iso", out);

    // 3. Origin-relative URLs
    TEST_ASSERT_EQUAL_INT(
        1, resolve_target_url("https://example.com/browse/38600",
                              "/file/38600/disc.iso", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://example.com/file/38600/disc.iso", out);

    // 4. Path-relative with trailing slash
    TEST_ASSERT_EQUAL_INT(1,
                          resolve_target_url("https://example.com/dir/",
                                             "sub/file.txt", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://example.com/dir/sub/file.txt", out);

    // 5. Path-relative without trailing slash
    TEST_ASSERT_EQUAL_INT(1,
                          resolve_target_url("https://example.com/dir/page",
                                             "sub/file.txt", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://example.com/dir/sub/file.txt", out);

    // 6. Path-relative on origin root
    TEST_ASSERT_EQUAL_INT(1, resolve_target_url("https://example.com",
                                                "file.txt", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://example.com/file.txt", out);

    // 7. Fragment stripping and whitespace trimming
    TEST_ASSERT_EQUAL_INT(1,
                          resolve_target_url("https://example.com/",
                                             "  /browse/38600?v=1#comments  ",
                                             out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://example.com/browse/38600?v=1", out);

    // 8. Intra-page fragment link alone
    TEST_ASSERT_EQUAL_INT(0, resolve_target_url("https://example.com/",
                                                "#section", out, sizeof(out)));

    // 9. Null or empty
    TEST_ASSERT_EQUAL_INT(0,
                          resolve_target_url(NULL, "/path", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(
        0, resolve_target_url("https://example.com/", NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(
        0, resolve_target_url("https://example.com/", "", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(
        0, resolve_target_url("https://example.com/", "   ", out, sizeof(out)));

    // 10. Raw spaces in href percent-encoded as %20
    TEST_ASSERT_EQUAL_INT(
        1, resolve_target_url(
               "https://example.com/browse/1001",
               "/browse/1001/Sample Archive - Collection 1.0.iso/001", out,
               sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://example.com/browse/1001/"
                             "Sample%20Archive%20-%20Collection%201.0.iso/001",
                             out);

    // 11. Dot segment normalization (RFC 3986 5.2.4)
    TEST_ASSERT_EQUAL_INT(1, resolve_target_url("https://example.com/A/B/C/",
                                                "../../", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://example.com/A/", out);

    TEST_ASSERT_EQUAL_INT(1, resolve_target_url("https://example.com/A/B/C/",
                                                "../", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://example.com/A/B/", out);

    TEST_ASSERT_EQUAL_INT(1, resolve_target_url("https://example.com/A/B/C/",
                                                ".", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://example.com/A/B/C/", out);

    TEST_ASSERT_EQUAL_INT(1, resolve_target_url("https://example.com/A/B/",
                                                "sub/../file.txt", out,
                                                sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://example.com/A/B/file.txt", out);
}

void test_extract_anchor_text(void)
{
    // Plain text
    GumboOutput *out = gumbo_parse("<a href='/test'>readme.txt</a>");
    char *text = extract_anchor_text(find_anchor_node(out->root));
    TEST_ASSERT_EQUAL_STRING("readme.txt", text);
    FREE(text);
    gumbo_destroy_output(&kGumboDefaultOptions, out);

    // Nested tags
    out = gumbo_parse("<a href='/test'><b>Download</b> <span>File</span> "
                      "<code>v1</code></a>");
    text = extract_anchor_text(find_anchor_node(out->root));
    TEST_ASSERT_EQUAL_STRING("Download File v1", text);
    FREE(text);
    gumbo_destroy_output(&kGumboDefaultOptions, out);

    // Entities and sanitization
    out = gumbo_parse(
        "<a href='/test'> &lt;Disc &amp; Sleeve/Cover&gt; &#39;A&#39; "
        "&quot;B&quot; &nbsp; </a>");
    text = extract_anchor_text(find_anchor_node(out->root));
    TEST_ASSERT_EQUAL_STRING("<Disc & Sleeve_Cover> 'A' \"B\"", text);
    FREE(text);
    gumbo_destroy_output(&kGumboDefaultOptions, out);

    // Empty anchor or image only
    out = gumbo_parse("<a href='/test'><img src='icon.png' /></a>");
    text = extract_anchor_text(find_anchor_node(out->root));
    TEST_ASSERT_EQUAL_STRING("", text);
    FREE(text);
    gumbo_destroy_output(&kGumboDefaultOptions, out);

    // Multiple consecutive literal spaces preserved
    out = gumbo_parse("<a href='/test'>multiple   spaces   here.bin</a>");
    text = extract_anchor_text(find_anchor_node(out->root));
    TEST_ASSERT_EQUAL_STRING("multiple   spaces   here.bin", text);
    FREE(text);
    gumbo_destroy_output(&kGumboDefaultOptions, out);

    // Multiline formatting whitespace collapsed
    out = gumbo_parse("<a href='/test'>\n  Download\n  File\n</a>");
    text = extract_anchor_text(find_anchor_node(out->root));
    TEST_ASSERT_EQUAL_STRING("Download File", text);
    FREE(text);
    gumbo_destroy_output(&kGumboDefaultOptions, out);

    // NULL node
    text = extract_anchor_text(NULL);
    TEST_ASSERT_EQUAL_STRING("", text);
    FREE(text);
}

void test_extract_url_path_segments(void)
{
    char **segs = NULL;
    int count = 0;

    // Multi-segment with trailing slash
    count = extract_url_path_segments("https://example.com/browse/38600/",
                                      &segs, &count);
    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_EQUAL_STRING("browse", segs[0]);
    TEST_ASSERT_EQUAL_STRING("38600", segs[1]);
    free_url_path_segments(segs, count);

    // Query string and fragment
    count = extract_url_path_segments(
        "https://example.com/file/38600/disc.iso?v=1#download", &segs, &count);
    TEST_ASSERT_EQUAL_INT(3, count);
    TEST_ASSERT_EQUAL_STRING("file", segs[0]);
    TEST_ASSERT_EQUAL_STRING("38600", segs[1]);
    TEST_ASSERT_EQUAL_STRING("disc.iso", segs[2]);
    free_url_path_segments(segs, count);

    // Encoded spaces and characters
    count = extract_url_path_segments("/archive/my%20file%20name.txt", &segs,
                                      &count);
    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_EQUAL_STRING("archive", segs[0]);
    TEST_ASSERT_EQUAL_STRING("my file name.txt", segs[1]);
    free_url_path_segments(segs, count);

    // Encoded slashes sanitized to underscores
    count = extract_url_path_segments("/archive/a%2Fb", &segs, &count);
    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_EQUAL_STRING("archive", segs[0]);
    TEST_ASSERT_EQUAL_STRING("a_b", segs[1]);
    free_url_path_segments(segs, count);

    // Root URL has 0 segments
    count = extract_url_path_segments("https://example.com/", &segs, &count);
    TEST_ASSERT_EQUAL_INT(0, count);
    TEST_ASSERT_NULL(segs);
}

void test_generate_collision_free_name(void)
{
    LinkHashSet *set = LinkHashSet_new(64);

    // Candidate 1: anchor differs from last component
    char *segs1[] = {"browse", "38600"};
    char *name1 = generate_collision_free_name(set, "001", segs1, 2);
    TEST_ASSERT_EQUAL_STRING("001-38600", name1);
    FREE(name1);

    // Candidate 1: anchor identical to last component (case-insensitive) ->
    // omit anchor
    char *segs2[] = {"file", "38600", "Disc.iso"};
    char *name2 = generate_collision_free_name(set, "disc.iso", segs2, 3);
    TEST_ASSERT_EQUAL_STRING("Disc.iso", name2);
    FREE(name2);

    // Candidate 1: anchor matches last component with different space count ->
    // omit anchor
    char *segs_sp[] = {"multiple   spaces   here.bin"};
    char *name_sp = generate_collision_free_name(
        set, "multiple spaces here.bin", segs_sp, 1);
    TEST_ASSERT_EQUAL_STRING("multiple   spaces   here.bin", name_sp);
    FREE(name_sp);

    // Candidate 1: anchor is empty
    char *segs3[] = {"images", "logo.png"};
    char *name3 = generate_collision_free_name(set, "", segs3, 2);
    TEST_ASSERT_EQUAL_STRING("logo.png", name3);
    FREE(name3);

    // Collision resolution with backward escalation:
    // First: "Download-disc.iso"
    char *segs_dl1[] = {"file", "38600", "disc.iso"};
    char *name_dl1 = generate_collision_free_name(set, "Download", segs_dl1, 3);
    TEST_ASSERT_EQUAL_STRING("Download-disc.iso", name_dl1);
    FREE(name_dl1);

    // Second: clashes on "Download-disc.iso", escalates to 2nd component ->
    // "Download-38601-disc.iso"
    char *segs_dl2[] = {"file", "38601", "disc.iso"};
    char *name_dl2 = generate_collision_free_name(set, "Download", segs_dl2, 3);
    TEST_ASSERT_EQUAL_STRING("Download-38601-disc.iso", name_dl2);
    FREE(name_dl2);

    // Third: clashes on "Download-disc.iso", then "Download-38601-disc.iso",
    // escalates to 3rd component -> "Download-archive-38601-disc.iso"
    char *segs_dl3[] = {"archive", "38601", "disc.iso"};
    char *name_dl3 = generate_collision_free_name(set, "Download", segs_dl3, 3);
    TEST_ASSERT_EQUAL_STRING("Download-archive-38601-disc.iso", name_dl3);
    FREE(name_dl3);

    // Complete exhaustion fallback to numeric suffix:
    char *segs_dl3_dup[] = {"archive", "38601", "disc.iso"};
    char *name_dl3_suffixed
        = generate_collision_free_name(set, "Download", segs_dl3_dup, 3);
    TEST_ASSERT_EQUAL_STRING("Download-archive-38601-disc.iso-1",
                             name_dl3_suffixed);
    FREE(name_dl3_suffixed);

    // Preceding dots in anchor stripped to avoid hidden files/folders
    char *segs_dots[] = {"browse", "38600"};
    char *name_dots1
        = generate_collision_free_name(set, "...002", segs_dots, 2);
    TEST_ASSERT_EQUAL_STRING("002-38600", name_dots1);
    FREE(name_dots1);

    char *name_dots2
        = generate_collision_free_name(set, ".hidden", segs_dots, 2);
    TEST_ASSERT_EQUAL_STRING("hidden-38600", name_dots2);
    FREE(name_dots2);

    char *name_dots3
        = generate_collision_free_name(set, "...  Folder", segs_dots, 2);
    TEST_ASSERT_EQUAL_STRING("Folder-38600", name_dots3);
    FREE(name_dots3);

    // Anchor consisting only of dots: omitted, fallback to path_part
    char *name_dots4 = generate_collision_free_name(set, "...", segs_dots, 2);
    TEST_ASSERT_EQUAL_STRING("38600", name_dots4);
    FREE(name_dots4);

    // Interleaved dots and spaces: both stripped simultaneously
    char *name_dots5
        = generate_collision_free_name(set, ". . . Mixed", segs_dots, 2);
    TEST_ASSERT_EQUAL_STRING("Mixed-38600", name_dots5);
    FREE(name_dots5);

    char *segs_dots2[] = {"browse", "38601"};
    char *name_dots6
        = generate_collision_free_name(set, " . . .", segs_dots2, 2);
    TEST_ASSERT_EQUAL_STRING("38601", name_dots6);
    FREE(name_dots6);

    LinkHashSet_free(set);
}

void test_is_html_content_type(void)
{
    TEST_ASSERT_EQUAL_INT(1, is_html_content_type("text/html"));
    TEST_ASSERT_EQUAL_INT(1, is_html_content_type("text/html; charset=utf-8"));
    TEST_ASSERT_EQUAL_INT(
        1, is_html_content_type("TEXT/HTML; CHARSET=ISO-8859-1"));
    TEST_ASSERT_EQUAL_INT(1, is_html_content_type("  text/html  "));
    TEST_ASSERT_EQUAL_INT(0, is_html_content_type("text/htmlxyz"));
    TEST_ASSERT_EQUAL_INT(0, is_html_content_type("application/octet-stream"));
    TEST_ASSERT_EQUAL_INT(0, is_html_content_type("image/png"));
    TEST_ASSERT_EQUAL_INT(0, is_html_content_type("text/plain"));
    TEST_ASSERT_EQUAL_INT(0, is_html_content_type(""));
    TEST_ASSERT_EQUAL_INT(0, is_html_content_type(NULL));
}

void test_Link_classify_response(void)
{
    size_t out_len = 0;

    // 1. Default mode (website_mode = 0)
    CONFIG.website_mode = 0;
    CONFIG.zero_len_is_dir = 0;
    TEST_ASSERT_EQUAL_INT(LINK_FILE,
                          Link_classify_response(LINK_UNINITIALISED_FILE, 200,
                                                 1000, "text/html", &out_len));
    TEST_ASSERT_EQUAL_INT(1000, (int)out_len);
    // flag off + HTML + unknown size -> tentative directory (size is learned
    // on first browse; an oversized or failed download degrades to an empty
    // folder)
    TEST_ASSERT_EQUAL_INT(LINK_DIR,
                          Link_classify_response(LINK_UNINITIALISED_FILE, 200,
                                                 -1, "text/html", &out_len));
    // flag off + non-HTML + unknown size -> hidden (LINK_INVALID)
    TEST_ASSERT_EQUAL_INT(LINK_INVALID,
                          Link_classify_response(LINK_UNINITIALISED_FILE, 200,
                                                 -1, "image/png", &out_len));
    // flag off + no content type + unknown size -> tentative directory
    // (on-the-fly generated listings often have neither Content-Type nor
    // Content-Length)
    TEST_ASSERT_EQUAL_INT(LINK_DIR,
                          Link_classify_response(LINK_UNINITIALISED_FILE, 200,
                                                 -1, NULL, &out_len));
    // non-200 -> invalid regardless of size
    TEST_ASSERT_EQUAL_INT(LINK_INVALID,
                          Link_classify_response(LINK_UNINITIALISED_FILE, 404,
                                                 1000, "text/html", &out_len));

    // Zero-len is dir
    CONFIG.zero_len_is_dir = 1;
    TEST_ASSERT_EQUAL_INT(LINK_DIR,
                          Link_classify_response(LINK_UNINITIALISED_FILE, 200,
                                                 0, "text/html", &out_len));

    // 2. HTML as directory mode (website_mode = 1)
    CONFIG.website_mode = 1;
    CONFIG.max_html_size = 2097152; // 2 MiB

    // HTML <= max_html_size -> LINK_DIR
    TEST_ASSERT_EQUAL_INT(
        LINK_DIR, Link_classify_response(LINK_UNINITIALISED_FILE, 200, 50000,
                                         "text/html; charset=utf-8", &out_len));
    TEST_ASSERT_EQUAL_INT(LINK_DIR,
                          Link_classify_response(LINK_UNINITIALISED_FILE, 200,
                                                 0, "text/html", &out_len));

    // HTML chunked (-1) -> tentative LINK_DIR
    TEST_ASSERT_EQUAL_INT(LINK_DIR,
                          Link_classify_response(LINK_UNINITIALISED_FILE, 200,
                                                 -1, "text/html", &out_len));

    // HTML > max_html_size -> LINK_FILE (readable file fallback)
    TEST_ASSERT_EQUAL_INT(
        LINK_FILE, Link_classify_response(LINK_UNINITIALISED_FILE, 200, 5000000,
                                          "text/html", &out_len));
    TEST_ASSERT_EQUAL_INT(5000000, (int)out_len);

    // Non-HTML (e.g. image, ISO) -> LINK_FILE
    TEST_ASSERT_EQUAL_INT(LINK_FILE,
                          Link_classify_response(LINK_UNINITIALISED_FILE, 200,
                                                 50000, "image/png", &out_len));
    TEST_ASSERT_EQUAL_INT(50000, (int)out_len);

    // flag on + non-HTML + unknown size -> hidden (a concrete non-HTML type is
    // trusted to be a file, not a listing, so it is never parsed; and its size
    // is unknown, so it cannot be presented as a file either)
    TEST_ASSERT_EQUAL_INT(LINK_INVALID,
                          Link_classify_response(LINK_UNINITIALISED_FILE, 200,
                                                 -1, "image/png", &out_len));

    // flag on + no content type + unknown size -> tentative directory
    TEST_ASSERT_EQUAL_INT(LINK_DIR,
                          Link_classify_response(LINK_UNINITIALISED_FILE, 200,
                                                 -1, NULL, &out_len));

    // Directory types preserved
    TEST_ASSERT_EQUAL_INT(LINK_DIR,
                          Link_classify_response(LINK_UNINITIALISED_DIR, 200, 0,
                                                 "text/html", &out_len));

    // Reset config
    CONFIG.website_mode = 0;
    CONFIG.zero_len_is_dir = 0;
}

void test_write_memory_capped_callback(void)
{
    char buf[64];
    memset(buf, 'a', sizeof(buf));

    // Capped at 100 bytes: the first 60-byte chunk fits, the second would
    // push the buffer past the cap, so the callback aborts (returns 0) and
    // flags cap_hit without buffering the excess.
    TransferStruct ts = {0};
    ts.size_cap = 100;
    ts.cap_hit = 0;

    size_t r1 = write_memory_capped_callback(buf, 1, 60, &ts);
    TEST_ASSERT_EQUAL_INT(60, (int)r1);
    TEST_ASSERT_EQUAL_INT(0, ts.cap_hit);
    TEST_ASSERT_EQUAL_INT(60, (int)ts.curr_size);

    size_t r2 = write_memory_capped_callback(buf, 1, 60, &ts);
    TEST_ASSERT_EQUAL_INT(0, (int)r2);
    TEST_ASSERT_EQUAL_INT(1, ts.cap_hit);
    TEST_ASSERT_EQUAL_INT(60, (int)ts.curr_size);
    TEST_ASSERT_NOT_NULL(ts.data);
    FREE(ts.data);

    // No cap (size_cap = 0): buffers the full body, never aborts.
    TransferStruct ts2 = {0};
    ts2.size_cap = 0;
    ts2.cap_hit = 0;
    size_t r3 = write_memory_capped_callback(buf, 1, 60, &ts2);
    TEST_ASSERT_EQUAL_INT(60, (int)r3);
    TEST_ASSERT_EQUAL_INT(0, ts2.cap_hit);
    TEST_ASSERT_EQUAL_INT(60, (int)ts2.curr_size);
    size_t r4 = write_memory_capped_callback(buf, 1, 60, &ts2);
    TEST_ASSERT_EQUAL_INT(60, (int)r4);
    TEST_ASSERT_EQUAL_INT(0, ts2.cap_hit);
    TEST_ASSERT_EQUAL_INT(120, (int)ts2.curr_size);
    FREE(ts2.data);
}

void test_unified_parsing_HTML_to_LinkTable(void)
{
    CONFIG.allow_external_origin = 1;

    LinkTable *tbl = LinkTable_alloc("https://example.com/browse/38600");
    TEST_ASSERT_NOT_NULL(tbl);

    const char *html
        = "<html><body>"
          "<a href='/browse/38600/sub'>001 Subdir</a>"
          "<a href='/browse/38600/sub'>Duplicate Link with Different Text</a>"
          "<a href='/file/38600/readme.txt'>Readme</a>"
          "<a href='/file/38601/readme.txt'>Readme</a>"
          "<a href='/file/38602/disc.iso'>DISC.ISO</a>"
          "<a href='https://other.server.com/external.iso'>External ISO</a>"
          "</body></html>";

    LinkTable_parse_html(tbl, "https://example.com/browse/38600", html);

    // Total links: 1 root + 5 unique links (duplicate '/browse/38600/sub' was
    // dropped!)
    TEST_ASSERT_EQUAL_INT(6, tbl->size);

    // Link 1: "001 Subdir-sub" (first anchor text kept!)
    TEST_ASSERT_EQUAL_STRING("001 Subdir-sub", tbl->links[1]->linkname);
    TEST_ASSERT_EQUAL_STRING("https://example.com/browse/38600/sub",
                             tbl->links[1]->f_url);

    // Link 2: anchor text is exactly Readme-readme.txt
    TEST_ASSERT_EQUAL_STRING("Readme-readme.txt", tbl->links[2]->linkname);
    TEST_ASSERT_EQUAL_STRING("https://example.com/file/38600/readme.txt",
                             tbl->links[2]->f_url);

    // Link 3: Clashed on "Readme-readme.txt" -> escalated to
    // "Readme-38601-readme.txt"
    TEST_ASSERT_EQUAL_STRING("Readme-38601-readme.txt",
                             tbl->links[3]->linkname);
    TEST_ASSERT_EQUAL_STRING("https://example.com/file/38601/readme.txt",
                             tbl->links[3]->f_url);

    // Link 4: "disc.iso" (anchor matched filename case-insensitively -> anchor
    // omitted)
    TEST_ASSERT_EQUAL_STRING("disc.iso", tbl->links[4]->linkname);
    TEST_ASSERT_EQUAL_STRING("https://example.com/file/38602/disc.iso",
                             tbl->links[4]->f_url);

    // Link 5: External ISO link permitted under allow_external_origin = 1
    TEST_ASSERT_EQUAL_STRING("External ISO-external.iso",
                             tbl->links[5]->linkname);
    TEST_ASSERT_EQUAL_STRING("https://other.server.com/external.iso",
                             tbl->links[5]->f_url);

    LinkTable_free(tbl);
    CONFIG.allow_external_origin = 0;
}

void test_parsing_allow_external_origin_disabled(void)
{
    CONFIG.allow_external_origin = 0;

    LinkTable *root_tbl = LinkTable_alloc("https://example.com/");
    ROOT_LINK_TBL = root_tbl;

    LinkTable *tbl = LinkTable_alloc("https://example.com/browse/38600");
    TEST_ASSERT_NOT_NULL(tbl);

    const char *html
        = "<html><body>"
          "<a href='/browse/38600/sub'>Subdir</a>"
          "<a href='https://other.server.com/external.iso'>External ISO</a>"
          "</body></html>";

    LinkTable_parse_html(tbl, "https://example.com/browse/38600", html);

    // External link must be omitted because allow_external_origin = 0!
    // Total links: 1 root + 1 same-origin link
    TEST_ASSERT_EQUAL_INT(2, tbl->size);
    TEST_ASSERT_EQUAL_STRING("Subdir-sub", tbl->links[1]->linkname);

    LinkTable_free(tbl);
    LinkTable_free(root_tbl);
    ROOT_LINK_TBL = NULL;
}

void test_is_ancestor_head_link_hierarchy(void)
{
    TEST_ASSERT_EQUAL_INT(
        0, is_ancestor_head_link(NULL, "https://example.com/A/"));
    LinkTable *tbl_a = LinkTable_alloc("https://example.com/A/");
    TEST_ASSERT_EQUAL_INT(0, is_ancestor_head_link(tbl_a, NULL));
    TEST_ASSERT_EQUAL_INT(0, is_ancestor_head_link(tbl_a, ""));

    LinkTable *tbl_b = LinkTable_alloc("https://example.com/A/B/");
    tbl_b->parent_tbl = tbl_a;

    LinkTable *tbl_c = LinkTable_alloc("https://example.com/A/B/C/");
    tbl_c->parent_tbl = tbl_b;

    // Self matches
    TEST_ASSERT_EQUAL_INT(
        1, is_ancestor_head_link(tbl_c, "https://example.com/A/B/C/"));
    TEST_ASSERT_EQUAL_INT(
        1, is_ancestor_head_link(tbl_c, "https://example.com/A/B/C"));

    // Parent B matches
    TEST_ASSERT_EQUAL_INT(
        1, is_ancestor_head_link(tbl_c, "https://example.com/A/B/"));
    TEST_ASSERT_EQUAL_INT(
        1, is_ancestor_head_link(tbl_c, "https://example.com/A/B"));

    // Grandparent A matches
    TEST_ASSERT_EQUAL_INT(
        1, is_ancestor_head_link(tbl_c, "https://example.com/A/"));
    TEST_ASSERT_EQUAL_INT(
        1, is_ancestor_head_link(tbl_c, "https://example.com/A"));

    // With query params (if parent has no query, candidate query is ignored)
    TEST_ASSERT_EQUAL_INT(
        1, is_ancestor_head_link(tbl_c, "https://example.com/A/?sort=name"));

    // Encoded space comparison
    LinkTable *tbl_space
        = LinkTable_alloc("https://example.com/A/Folder%20Name/");
    tbl_space->parent_tbl = tbl_a;
    TEST_ASSERT_EQUAL_INT(
        1,
        is_ancestor_head_link(tbl_space, "https://example.com/A/Folder Name/"));
    TEST_ASSERT_EQUAL_INT(
        1, is_ancestor_head_link(tbl_space,
                                 "https://example.com/A/Folder%20Name"));

    // Child does not match
    TEST_ASSERT_EQUAL_INT(
        0, is_ancestor_head_link(tbl_c, "https://example.com/A/B/C/D/"));
    TEST_ASSERT_EQUAL_INT(
        0, is_ancestor_head_link(tbl_c, "https://example.com/A/B/C/file.txt"));
    // Unrelated does not match
    TEST_ASSERT_EQUAL_INT(
        0, is_ancestor_head_link(tbl_c, "https://example.com/other/"));

    LinkTable_free(tbl_space);
    LinkTable_free(tbl_c);
    LinkTable_free(tbl_b);
    LinkTable_free(tbl_a);
}

void test_discard_ancestor_links_in_parse_html(void)
{
    LinkTable *tbl_a = LinkTable_alloc("https://example.com/A/");
    LinkTable *tbl_b = LinkTable_alloc("https://example.com/A/B/");
    tbl_b->parent_tbl = tbl_a;
    LinkTable *tbl_c = LinkTable_alloc("https://example.com/A/B/C/");
    tbl_c->parent_tbl = tbl_b;

    const char *html = "<html><body>"
                       "<a href='/A'>Home A</a>"
                       "<a href='/A/B/'>Parent B</a>"
                       "<a href='/A/B/C/'>Current C</a>"
                       "<a href='../../'>DotDot to A</a>"
                       "<a href='../'>Dot to B</a>"
                       "<a href='.'>Dot to C</a>"
                       "<a href='/A/B/C/child_dir/'>Valid Child</a>"
                       "<a href='/A/B/C/file.iso'>file.iso</a>"
                       "</body></html>";

    LinkTable_parse_html(tbl_c, "https://example.com/A/B/C/", html);

    // tbl_c should contain:
    // index 0: head link "/"
    // index 1: "Valid Child-child_dir"
    // index 2: "file.iso"
    // All links pointing to A, B, or C (both absolute and relative) must be
    // discarded!
    TEST_ASSERT_EQUAL_INT(3, tbl_c->size);
    TEST_ASSERT_EQUAL_STRING("Valid Child-child_dir",
                             tbl_c->links[1]->linkname);
    TEST_ASSERT_EQUAL_STRING("file.iso", tbl_c->links[2]->linkname);

    LinkTable_free(tbl_c);
    LinkTable_free(tbl_b);
    LinkTable_free(tbl_a);
}

void test_discard_ancestor_links_relative_parent(void)
{
    LinkTable *tbl_a = LinkTable_alloc("https://example.com/A/");
    LinkTable *tbl_b = LinkTable_alloc("https://example.com/A/B/");
    tbl_b->parent_tbl = tbl_a;

    const char *html = "<html><body>"
                       "<a href='/A/'>Parent A</a>"
                       "<a href='../'>Parent A via dotdot</a>"
                       "<a href='child/'>Child</a>"
                       "</body></html>";

    LinkTable_parse_html(tbl_b, "https://example.com/A/B/", html);

    // Links to parent A must be discarded!
    // tbl_b should only have head link and child
    TEST_ASSERT_EQUAL_INT(2, tbl_b->size);
    TEST_ASSERT_EQUAL_STRING("child", tbl_b->links[1]->linkname);

    LinkTable_free(tbl_b);
    LinkTable_free(tbl_a);
}

/* ========================================================================= */
/* Non-http(s) scheme rejection in resolve_target_url()                      */
/* ========================================================================= */

void test_resolve_target_url_non_http_schemes(void)
{
    char out[1024];
    const char *page = "https://example.com/dir/";

    TEST_ASSERT_EQUAL_INT(0,
                          resolve_target_url(page, "data:image/png;base64,AAAA",
                                             out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(
        0, resolve_target_url(page, "javascript:alert(1)", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(0, resolve_target_url(page, "mailto:foo@example.com",
                                                out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(0, resolve_target_url(page,
                                                "blob:https://example.com/uuid",
                                                out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(
        0, resolve_target_url(page, "tel:+123456", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(0, resolve_target_url(page, "FTP://example.com/f.iso",
                                                out, sizeof(out)));

    /* http(s) stays resolvable, scheme check is case-insensitive */
    TEST_ASSERT_EQUAL_INT(1, resolve_target_url(page, "http://other.org/f.iso",
                                                out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("http://other.org/f.iso", out);
    TEST_ASSERT_EQUAL_INT(1, resolve_target_url(page, "HTTPS://other.org/f.iso",
                                                out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("HTTPS://other.org/f.iso", out);

    /* A colon after the first '/' is not a scheme */
    TEST_ASSERT_EQUAL_INT(
        1, resolve_target_url(page, "sub/file:copy.iso", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://example.com/dir/sub/file:copy.iso", out);
}

/* ========================================================================= */
/* Media resource extraction tests (img / srcset / video / css / ...)        */
/* ========================================================================= */

static const Link *find_link_by_url(LinkTable *tbl, const char *url)
{
    for (int i = 1; i < tbl->size; i++) {
        if (strcmp(tbl->links[i]->f_url, url) == 0) {
            return tbl->links[i];
        }
    }
    return NULL;
}

void test_resource_img_basic(void)
{
    LinkTable *tbl = LinkTable_alloc("https://example.com/dir/");

    LinkTable_parse_html(tbl, "https://example.com/dir/",
                         "<html><body>"
                         "<img src=\"cat.png\" alt=\"My Cat\">"
                         "</body></html>");

    /* 1 head + 1 image link, named from the alt text */
    TEST_ASSERT_EQUAL_INT(2, tbl->size);
    TEST_ASSERT_EQUAL_STRING("My Cat-cat.png", tbl->links[1]->linkname);
    TEST_ASSERT_EQUAL_STRING("https://example.com/dir/cat.png",
                             tbl->links[1]->f_url);
    TEST_ASSERT_EQUAL_INT(LINK_UNINITIALISED_FILE, tbl->links[1]->type);

    LinkTable_free(tbl);
}

void test_resource_img_no_alt_and_empty_alt(void)
{
    LinkTable *tbl = LinkTable_alloc("https://example.com/dir/");

    LinkTable_parse_html(tbl, "https://example.com/dir/",
                         "<html><body>"
                         "<img src=\"cat.png\">"
                         "<img src=\"dog.png\" alt=\"   \">"
                         "</body></html>");

    /* Without usable alt text the names fall back to the URL filename */
    TEST_ASSERT_EQUAL_INT(3, tbl->size);
    TEST_ASSERT_EQUAL_STRING("cat.png", tbl->links[1]->linkname);
    TEST_ASSERT_EQUAL_STRING("dog.png", tbl->links[2]->linkname);

    LinkTable_free(tbl);
}

void test_resource_img_duplicate_alt_falls_back(void)
{
    LinkTable *tbl = LinkTable_alloc("https://example.com/dir/");

    /* alt "Image" is reused -> generic filler, both fall back to filenames */
    LinkTable_parse_html(tbl, "https://example.com/dir/",
                         "<html><body>"
                         "<img src=\"a.png\" alt=\"Image\">"
                         "<img src=\"b.png\" alt=\"Image\">"
                         "</body></html>");

    TEST_ASSERT_EQUAL_INT(3, tbl->size);
    TEST_ASSERT_EQUAL_STRING("a.png", tbl->links[1]->linkname);
    TEST_ASSERT_EQUAL_STRING("b.png", tbl->links[2]->linkname);

    LinkTable_free(tbl);
}

void test_resource_img_unique_anchors(void)
{
    LinkTable *tbl = LinkTable_alloc("https://example.com/dir/");

    /* Distinct alt texts are used as naming anchors */
    LinkTable_parse_html(tbl, "https://example.com/dir/",
                         "<html><body>"
                         "<img src=\"a.png\" alt=\"First\">"
                         "<img src=\"b.png\" alt=\"Second\">"
                         "</body></html>");

    TEST_ASSERT_EQUAL_INT(3, tbl->size);
    TEST_ASSERT_EQUAL_STRING("First-a.png", tbl->links[1]->linkname);
    TEST_ASSERT_EQUAL_STRING("Second-b.png", tbl->links[2]->linkname);

    LinkTable_free(tbl);
}

void test_resource_srcset_candidates(void)
{
    LinkTable *tbl = LinkTable_alloc("https://example.com/dir/");

    LinkTable_parse_html(tbl, "https://example.com/dir/",
                         "<html><body>"
                         "<img srcset=\"small.jpg 480w, large.jpg 1024w, "
                         "https://example.com/abs/extra.jpg 2x\">"
                         "</body></html>");

    /* 1 head + 3 srcset candidates (descriptors stripped) */
    TEST_ASSERT_EQUAL_INT(4, tbl->size);
    const Link *l1 = find_link_by_url(tbl, "https://example.com/dir/small.jpg");
    TEST_ASSERT_NOT_NULL(l1);
    TEST_ASSERT_EQUAL_STRING("small.jpg", l1->linkname);
    const Link *l2 = find_link_by_url(tbl, "https://example.com/dir/large.jpg");
    TEST_ASSERT_NOT_NULL(l2);
    TEST_ASSERT_EQUAL_STRING("large.jpg", l2->linkname);
    const Link *l3 = find_link_by_url(tbl, "https://example.com/abs/extra.jpg");
    TEST_ASSERT_NOT_NULL(l3);
    TEST_ASSERT_EQUAL_STRING("extra.jpg", l3->linkname);

    LinkTable_free(tbl);
}

void test_resource_media_and_asset_tags(void)
{
    LinkTable *tbl = LinkTable_alloc("https://example.com/dir/");

    LinkTable_parse_html(
        tbl, "https://example.com/dir/",
        "<html><head>"
        "<link rel=\"stylesheet\" href=\"style.css\">"
        "<link rel=\"icon\" href=\"favicon.ico\">"
        "<script src=\"app.js\"></script>"
        "</head><body>"
        "<video src=\"movie.mp4\"></video>"
        "<audio src=\"song.mp3\"></audio>"
        "<source src=\"alt.webm\">"
        "<iframe src=\"frame.html\"></iframe>"
        "<object data=\"plugin.swf\"></object>"
        "<embed src=\"widget.swf\">"
        "<track src=\"subs.vtt\">"
        "<input type=\"image\" src=\"button.png\">"
        "<map name=\"m\"><area href=\"area_target.html\" shape=\"rect\"></map>"
        /* Not resource references: must not be extracted */
        "<input type=\"text\" src=\"ignored.png\">"
        "<form action=\"form_target.html\"></form>"
        "</body></html>");

    const char *expected[] = {
        "https://example.com/dir/style.css",
        "https://example.com/dir/favicon.ico",
        "https://example.com/dir/app.js",
        "https://example.com/dir/movie.mp4",
        "https://example.com/dir/song.mp3",
        "https://example.com/dir/alt.webm",
        "https://example.com/dir/frame.html",
        "https://example.com/dir/plugin.swf",
        "https://example.com/dir/widget.swf",
        "https://example.com/dir/subs.vtt",
        "https://example.com/dir/button.png",
        "https://example.com/dir/area_target.html",
    };
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        TEST_ASSERT_NOT_NULL(find_link_by_url(tbl, expected[i]));
    }
    TEST_ASSERT_NULL(
        find_link_by_url(tbl, "https://example.com/dir/ignored.png"));
    TEST_ASSERT_NULL(
        find_link_by_url(tbl, "https://example.com/dir/form_target.html"));

    /* 1 head + 12 extracted resources */
    TEST_ASSERT_EQUAL_INT(13, tbl->size);

    LinkTable_free(tbl);
}

void test_resource_dedup_shared_with_anchor(void)
{
    LinkTable *tbl = LinkTable_alloc("https://example.com/dir/");

    /* Same target via <a> and <img>: one entry, first anchor text wins */
    LinkTable_parse_html(tbl, "https://example.com/dir/",
                         "<html><body>"
                         "<a href=\"cat.png\">The Cat</a>"
                         "<img src=\"cat.png\" alt=\"Also Cat\">"
                         "</body></html>");

    TEST_ASSERT_EQUAL_INT(2, tbl->size);
    TEST_ASSERT_EQUAL_STRING("The Cat-cat.png", tbl->links[1]->linkname);

    LinkTable_free(tbl);
}

void test_resource_non_http_schemes_skipped(void)
{
    LinkTable *tbl = LinkTable_alloc("https://example.com/dir/");

    LinkTable_parse_html(tbl, "https://example.com/dir/",
                         "<html><body>"
                         "<img src=\"data:image/png;base64,AAAA\">"
                         "<a href=\"mailto:foo@example.com\">Mail</a>"
                         "<a href=\"javascript:doit()\">JS</a>"
                         "</body></html>");

    /* Only the head link remains */
    TEST_ASSERT_EQUAL_INT(1, tbl->size);

    LinkTable_free(tbl);
}

void test_resource_cross_origin_filtered(void)
{
    CONFIG.allow_external_origin = 0;
    LinkTable *tbl = LinkTable_alloc("https://example.com/dir/");
    LinkTable_parse_html(tbl, "https://example.com/dir/",
                         "<html><body>"
                         "<img src=\"https://other.example/img.png\">"
                         "</body></html>");
    TEST_ASSERT_EQUAL_INT(1, tbl->size);
    LinkTable_free(tbl);

    CONFIG.allow_external_origin = 1;
    tbl = LinkTable_alloc("https://example.com/dir/");
    LinkTable_parse_html(tbl, "https://example.com/dir/",
                         "<html><body>"
                         "<img src=\"https://other.example/img.png\">"
                         "</body></html>");
    TEST_ASSERT_EQUAL_INT(2, tbl->size);
    TEST_ASSERT_EQUAL_STRING("img.png", tbl->links[1]->linkname);
    LinkTable_free(tbl);
}

int main(void)
{
    UNITY_BEGIN();

    /* is_external_url */
    RUN_TEST(test_is_external_url_http);
    RUN_TEST(test_is_external_url_https);
    RUN_TEST(test_is_external_url_relative);
    RUN_TEST(test_is_external_url_absolute_path);
    RUN_TEST(test_is_external_url_ftp);
    RUN_TEST(test_is_external_url_null);
    RUN_TEST(test_is_external_url_uppercase);

    /* is_cross_origin */
    RUN_TEST(test_is_cross_origin_different_host);
    RUN_TEST(test_is_cross_origin_same_host);
    RUN_TEST(test_is_cross_origin_different_scheme);
    RUN_TEST(test_is_cross_origin_different_port);
    RUN_TEST(test_is_cross_origin_same_host_with_port);
    RUN_TEST(test_is_cross_origin_no_trailing_slash_same);
    RUN_TEST(test_is_cross_origin_no_trailing_slash_different);
    RUN_TEST(test_is_cross_origin_both_no_trailing_slash_same);
    RUN_TEST(test_is_cross_origin_null);
    RUN_TEST(test_is_cross_origin_case_insensitive);
    RUN_TEST(test_is_cross_origin_with_query_and_fragment);
    RUN_TEST(test_is_cross_origin_default_port_normalization_http);
    RUN_TEST(test_is_cross_origin_default_port_normalization_https);
    RUN_TEST(test_is_cross_origin_non_default_port_not_equal);

    /* HTML_to_LinkTable integration via LinkTable_parse_html */
    RUN_TEST(test_HTML_external_link_file);
    RUN_TEST(test_HTML_external_link_dir);
    RUN_TEST(test_HTML_external_link_disabled);
    RUN_TEST(test_HTML_external_link_same_origin);
    RUN_TEST(test_HTML_duplicate_target_url_first_wins);
    RUN_TEST(test_HTML_external_link_dot_and_dotdot);
    RUN_TEST(test_Link_preserves_preset_f_url);

    /* url_to_cache_path */
    RUN_TEST(test_url_to_cache_path_null);
    RUN_TEST(test_url_to_cache_path_local);
    RUN_TEST(test_url_to_cache_path_external_sanitization);
    RUN_TEST(test_url_to_cache_path_same_origin_different_path);
    RUN_TEST(test_url_to_cache_path_same_origin_encoding_divergence);
    RUN_TEST(test_url_to_cache_path_long_url_hashed);
    RUN_TEST(test_canonicalize_url);
    RUN_TEST(test_get_server_root);

    /* Pre-existing tests */
    RUN_TEST(test_LinkTable_alloc);
    RUN_TEST(test_LinkTable_add);
    RUN_TEST(test_Link_download_zero_length);
    RUN_TEST(test_link_linknames_equal);
    RUN_TEST(test_link_hash_str);
    RUN_TEST(test_LinkHashSet);
    RUN_TEST(test_LinkTable_parse_html_duplicates);

    /* ignore_anchors */
    RUN_TEST(test_ignore_anchors_default);
    RUN_TEST(test_ignore_anchors_enabled);

    /* diagnostics (.httpdirfs) */
    RUN_TEST(test_diagnostics_add);
    RUN_TEST(test_diagnostics_path_lookup_and_read);
    RUN_TEST(test_diagnostics_subdirectory);
    RUN_TEST(test_diagnostics_empty);
    RUN_TEST(test_LinkTable_expired_subdirectory);

    /* Unified link parsing and directory detection */
    RUN_TEST(test_resolve_target_url);
    RUN_TEST(test_extract_anchor_text);
    RUN_TEST(test_extract_url_path_segments);
    RUN_TEST(test_generate_collision_free_name);
    RUN_TEST(test_is_html_content_type);
    RUN_TEST(test_Link_classify_response);
    RUN_TEST(test_write_memory_capped_callback);
    RUN_TEST(test_unified_parsing_HTML_to_LinkTable);
    RUN_TEST(test_parsing_allow_external_origin_disabled);
    RUN_TEST(test_is_ancestor_head_link_hierarchy);
    RUN_TEST(test_discard_ancestor_links_in_parse_html);
    RUN_TEST(test_discard_ancestor_links_relative_parent);

    /* Non-http(s) scheme rejection */
    RUN_TEST(test_resolve_target_url_non_http_schemes);

    /* Media resource extraction */
    RUN_TEST(test_resource_img_basic);
    RUN_TEST(test_resource_img_no_alt_and_empty_alt);
    RUN_TEST(test_resource_img_duplicate_alt_falls_back);
    RUN_TEST(test_resource_img_unique_anchors);
    RUN_TEST(test_resource_srcset_candidates);
    RUN_TEST(test_resource_media_and_asset_tags);
    RUN_TEST(test_resource_dedup_shared_with_anchor);
    RUN_TEST(test_resource_non_http_schemes_skipped);
    RUN_TEST(test_resource_cross_origin_filtered);

    return UNITY_END();
}
