#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include "../src/link.h"
#include "../src/util.h"
#include "../src/config.h"

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
    LinkTable *table = LinkTable_alloc("http://example.com/dir/");
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_EQUAL_INT(1, table->size); // Alloc adds the head link
    TEST_ASSERT_NOT_NULL(table->links);
    TEST_ASSERT_NOT_NULL(table->links[0]);
    TEST_ASSERT_EQUAL_STRING("http://example.com/dir/", table->links[0]->f_url);
    TEST_ASSERT_EQUAL_STRING("", table->links[0]->linkname);
    LinkTable_free(table);
}

void test_LinkTable_add(void)
{
    LinkTable *table = LinkTable_alloc("http://example.com/dir/");
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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_LinkTable_alloc);
    RUN_TEST(test_LinkTable_add);
    return UNITY_END();
}
