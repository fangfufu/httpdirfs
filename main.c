#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network.h"

#include "test.h"

void init()
{
    Network_init();
}

int main(int argc, char** argv)
{
    (void) argc;
    (void) argv;

    init();

    link_test();

    return 0;
}
