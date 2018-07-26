#include "network.h"
#include "fuse_local.h"

#include <stdio.h>

static void help();

int main(int argc, char **argv)
{
    /*
     * Copied from:
     * https://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/src/bbfs.c
     */
    if ((argc < 3) || (argv[argc-2][0] == '-') || (argv[argc-1][0] == '-')) {
        help();
    }

    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;

    char *BASE_URL = argv[argc-2];
    network_init(BASE_URL);

    fuse_local_init(argc, argv);

    return 0;
}

static void help()
{
    fprintf(stderr,
            "usage:  mount-http-dir [options] URL mount_point\n");
    exit(EXIT_FAILURE);
}

