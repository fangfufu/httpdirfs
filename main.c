#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "link.h"

#include "test.h"

void init()
{
    curl_global_init(CURL_GLOBAL_ALL);
}


int main(int argc, char** argv)
{
    (void) argc;
    (void) argv;

    init();

    gumbo_test();
    url_test();

    return 0;
}
