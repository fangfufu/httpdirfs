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
    // clean stuff up here
}

void test_LinkTable_alloc(void)
{
    LinkTable *table = LinkTable_alloc("https://example.com/dir/");
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_EQUAL_INT(1, table->size); // Alloc adds the head link
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
    RUN_TEST(test_LinkTable_alloc);
    RUN_TEST(test_LinkTable_add);
    RUN_TEST(test_Link_download_zero_length);
    RUN_TEST(test_link_linknames_equal);
    RUN_TEST(test_link_hash_str);
    RUN_TEST(test_LinkHashSet);
    RUN_TEST(test_LinkTable_parse_html_duplicates);
    return UNITY_END();
}
