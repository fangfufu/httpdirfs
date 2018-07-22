#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

/* must be included before including <fuse.h> */
#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "network.h"
#include "fuse_local.h"

static const char *hello_str = "Hello World!\n";
static const char *hello_path = "/hello";

static struct fuse_operations fs_oper = {
    .getattr	= fs_getattr,
    .readdir	= fs_readdir,
    .open		= fs_open,
    .read		= fs_read,
};

/** \brief return the attributes for a single file indicated by path */
static int fs_getattr(const char *path, struct stat *stbuf)
{
    int res = 0;

    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 100;
    } else if (strcmp(path, hello_path) == 0) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 100;
        stbuf->st_size = strlen(hello_str);
    } else
        res = -ENOENT;

    return res;
}

/** \brief read the directory indicated by the path*/
static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;

    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    filler(buf, hello_path + 1, NULL, 0);

    return 0;
}

/** \brief open a file indicated by the path */
static int fs_open(const char *path, struct fuse_file_info *fi)
{
    if (strcmp(path, hello_path) != 0)
        return -ENOENT;

    if ((fi->flags & 3) != O_RDONLY)
        return -EACCES;

    return 0;
}

/** \brief read a file */
static int fs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    size_t len;
    (void) fi;
    if(strcmp(path, hello_path) != 0)
        return -ENOENT;

    len = strlen(hello_str);
    if (offset < len) {
        if (offset + size > len)
            size = len - offset;
        memcpy(buf, hello_str + offset, size);
    } else
        size = 0;

    return size;
}
