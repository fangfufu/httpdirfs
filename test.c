#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "link.h"
#include "test.h"

void url_test()
{
    printf("--- start of url_test ---\n");
    char *url1 = "http://www.google.com/";
    char *url2 = "http://www.google.com";
    char *cat_url1 = url_append(url1, "fangfufu");
    char *cat_url2 = url_append(url2, "fangfufu");
    printf("%d %s\n", (int) strlen(cat_url1), cat_url1);
    printf("%d %s\n", (int) strlen(cat_url2), cat_url2);
    printf("--- end of url_test ---\n\n");
}

void gumbo_test()
{
    printf("--- start of gumbo_test ---\n");

    LinkTable *linktbl = LinkTable_new(
        "https://www.fangfufu.co.uk/~fangfufu/test/");

    LinkTable_print(linktbl);
    LinkTable_free(linktbl);
    printf("--- end of gumbo_test ---\n\n");
}
