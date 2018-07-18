#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"


int main(int argc, char** argv) {
    gumbo_test(argc, argv);
    url_test();
    return 0;
}
