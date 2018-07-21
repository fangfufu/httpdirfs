#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

#include "network.h"
#include "fuse_local.h"

static struct fuse_lowlevel_ops fs_ops = {
    .lookup		= fs_lookup,
    .getattr	= fs_getattr,
    .readdir	= fs_readdir,
    .open		= fs_open,
    .read		= fs_read,
};
