#include <unity.h>
#include <stdlib.h>
#include <string.h>
#include "../src/link.h"
#include "../src/util.h"

void setUp(void)
{
    // set stuff up here
}

void tearDown(void)
{
    // clean stuff up here
}

void test_LinkTable_alloc(void)
{
    LinkTable *table = LinkTable_alloc("http://example.com/dir/");
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_EQUAL_INT(1, table->num); // Alloc adds the head link
    LinkTable_free(table);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_LinkTable_alloc);
    return UNITY_END();
}
