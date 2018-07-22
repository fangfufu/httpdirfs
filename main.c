#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network.h"

void link_test()
{
    printf("--- start of link_test ---\n");
    LinkTable_print(ROOT_LINK_TBL);
    Link *link = path_to_Link("6/6.txt", ROOT_LINK_TBL);
    Link_download(link, 1, 20);
    printf(link->body);
    printf("\n--- end of link_test ---\n\n");
}

void init()
{
    network_init("http://127.0.0.1/~fangfufu/");
}

int main(int argc, char** argv)
{
    (void) argc;
    (void) argv;

    init();

    link_test();

    return 0;
}
