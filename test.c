#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "link.h"
#include "test.h"


void link_test()
{
    printf("--- start of link_test ---\n");

    LinkTable *linktbl = LinkTable_new(
        "http://127.0.0.1/~fangfufu/");

    LinkTable_print(linktbl);
    Link_download(linktbl->links[1], 1, 20);
    printf(linktbl->links[1]->body);
    LinkTable_free(linktbl);
    printf("\n--- end of link_test ---\n\n");
}
