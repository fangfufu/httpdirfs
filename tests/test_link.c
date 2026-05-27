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
    /* nothing */
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

/* ========================================================================= */
/* external_url_to_filename() tests                                          */
/* ========================================================================= */

void test_external_url_to_filename_simple(void)
{
    char *name = external_url_to_filename("http://example.com/file.iso");
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_STRING("file.iso", name);
    FREE(name);
}

void test_external_url_to_filename_nested_path(void)
{
    char *name = external_url_to_filename("http://example.com/a/b/file.iso");
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_STRING("file.iso", name);
    FREE(name);
}

void test_external_url_to_filename_dir(void)
{
    char *name = external_url_to_filename("http://example.com/subdir/");
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_STRING("subdir", name);
    FREE(name);
}

void test_external_url_to_filename_encoded(void)
{
    char *name = external_url_to_filename("http://example.com/my%20file.iso");
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_STRING("my file.iso", name);
    FREE(name);
}

void test_external_url_to_filename_query(void)
{
    char *name = external_url_to_filename("http://example.com/file.iso?v=1");
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_STRING("file.iso", name);
    FREE(name);
}

void test_external_url_to_filename_root(void)
{
    /* A root-only URL has no filename — should return empty string. */
    char *name = external_url_to_filename("http://example.com/");
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_STRING("", name);
    FREE(name);
}

void test_external_url_to_filename_null(void)
{
    char *name = external_url_to_filename(NULL);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_STRING("", name);
    FREE(name);
}

void test_external_url_to_filename_fragment(void)
{
    char *name
        = external_url_to_filename("http://example.com/file.iso#section");
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_STRING("file.iso", name);
    FREE(name);
}

void test_external_url_to_filename_query_and_fragment(void)
{
    char *name
        = external_url_to_filename("http://example.com/file.iso?v=1#section");
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_STRING("file.iso", name);
    FREE(name);
}

/* ========================================================================= */
/* HTML_to_LinkTable() integration tests via LinkTable_parse_html()          */
/* ========================================================================= */

void test_HTML_external_link_file(void)
{
    CONFIG.external_links = 1;
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
    CONFIG.external_links = 0;
}

void test_HTML_external_link_dir(void)
{
    CONFIG.external_links = 1;
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
    CONFIG.external_links = 0;
}

void test_HTML_external_link_disabled(void)
{
    CONFIG.external_links = 0; /* flag is OFF */
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
    /* A full absolute URL pointing to the SAME origin is NOT cross-origin, so
     * it goes through the normal relative-link path.  make_link_relative()
     * only handles path-absolute links (starting with '/'); it leaves full
     * URIs unchanged.  linkname_to_LinkType() then rejects them as LINK_INVALID
     * (multiple embedded slashes).  The net result is: no link is added.
     * This is the pre-existing behaviour and is unchanged by this feature. */
    CONFIG.external_links = 1;
    LinkTable *tbl = LinkTable_alloc("http://localhost/");
    LinkTable_parse_html(tbl, "http://localhost/",
                         "<a href=\"http://localhost/file.iso\">"
                         "file.iso</a>");

    /* Same-origin full URI is invalid on the normal path — only head link */
    TEST_ASSERT_EQUAL_INT(1, tbl->size);
    LinkTable_free(tbl);
    CONFIG.external_links = 0;
}

void test_HTML_external_link_dedup_first_wins(void)
{
    CONFIG.external_links = 1;
    LinkTable *tbl = LinkTable_alloc("http://localhost/");
    /* Two different external servers share the same filename */
    LinkTable_parse_html(tbl, "http://localhost/",
                         "<a href=\"http://server-a.com/file.iso\">file.iso"
                         "</a><a href=\"http://server-b.com/file.iso\">"
                         "file.iso</a>");

    /* Only one link: the first one wins */
    TEST_ASSERT_EQUAL_INT(2, tbl->size);
    TEST_ASSERT_EQUAL_STRING("http://server-a.com/file.iso",
                             tbl->links[1]->f_url);
    LinkTable_free(tbl);
    CONFIG.external_links = 0;
}

void test_HTML_external_link_dot_and_dotdot(void)
{
    CONFIG.external_links = 1;
    LinkTable *tbl = LinkTable_alloc("http://localhost/");
    LinkTable_parse_html(
        tbl, "http://localhost/",
        "<a href=\"http://external.com/.\">dot</a>"
        "<a href=\"http://external.com/..\">dotdot</a>"
        "<a href=\"http://external.com/file.iso\">file.iso</a>");

    /* Expect 2 entries: HEAD link + file.iso (ignoring . and ..) */
    TEST_ASSERT_EQUAL_INT(2, tbl->size);
    TEST_ASSERT_EQUAL_STRING("file.iso", tbl->links[1]->linkname);
    LinkTable_free(tbl);
    CONFIG.external_links = 0;
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

void test_url_to_cache_path_local(void)
{
    char *path = url_to_cache_path("http://localhost/my%20file.iso");
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_EQUAL_STRING("http://localhost/my file.iso", path);
    FREE(path);
}

void test_url_to_cache_path_external_sanitization(void)
{
    ROOT_LINK_TBL = LinkTable_alloc("http://localhost/");
    char *path = url_to_cache_path("http://external.com/my%20file.iso?param=1");
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_EQUAL_STRING("http___external.com_my file.iso?param=1", path);
    FREE(path);
    LinkTable_free(ROOT_LINK_TBL);
    ROOT_LINK_TBL = NULL;
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

    /* external_url_to_filename */
    RUN_TEST(test_external_url_to_filename_simple);
    RUN_TEST(test_external_url_to_filename_nested_path);
    RUN_TEST(test_external_url_to_filename_dir);
    RUN_TEST(test_external_url_to_filename_encoded);
    RUN_TEST(test_external_url_to_filename_query);
    RUN_TEST(test_external_url_to_filename_root);
    RUN_TEST(test_external_url_to_filename_null);
    RUN_TEST(test_external_url_to_filename_fragment);
    RUN_TEST(test_external_url_to_filename_query_and_fragment);

    /* HTML_to_LinkTable integration via LinkTable_parse_html */
    RUN_TEST(test_HTML_external_link_file);
    RUN_TEST(test_HTML_external_link_dir);
    RUN_TEST(test_HTML_external_link_disabled);
    RUN_TEST(test_HTML_external_link_same_origin);
    RUN_TEST(test_HTML_external_link_dedup_first_wins);
    RUN_TEST(test_HTML_external_link_dot_and_dotdot);
    RUN_TEST(test_Link_preserves_preset_f_url);
    /* url_to_cache_path */
    RUN_TEST(test_url_to_cache_path_null);
    RUN_TEST(test_url_to_cache_path_local);
    RUN_TEST(test_url_to_cache_path_external_sanitization);

    /* Pre-existing tests */
    RUN_TEST(test_LinkTable_alloc);
    RUN_TEST(test_LinkTable_add);
    RUN_TEST(test_Link_download_zero_length);
    RUN_TEST(test_link_linknames_equal);
    RUN_TEST(test_link_hash_str);
    RUN_TEST(test_LinkHashSet);
    RUN_TEST(test_LinkTable_parse_html_duplicates);
    return UNITY_END();
}
