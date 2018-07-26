#ifndef FUSE_LOCAL_H
#define FUSE_LOCAL_H

/* must be included before including <fuse.h> */
#define FUSE_USE_VERSION 26
#include <fuse.h>

int fuse_local_init(int argc, char **argv);

#endif
