#include "fuse_local.h"

#include "cache.h"
#include "link.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

static void *fs_init(struct fuse_conn_info *conn)
{
    (void) conn;
    return NULL;
}

/** \brief release an opened file */
static int fs_release(const char *path, struct fuse_file_info *fi)
{
    if (CACHE_SYSTEM_INIT) {
        Cache_close((Cache *)fi->fh);
    }
    fprintf(stderr, "fs_release(): %s\n", path);
    return 0;
}


/** \brief return the attributes for a single file indicated by path */
static int fs_getattr(const char *path, struct stat *stbuf)
{
    int res = 0;
    memset(stbuf, 0, sizeof(struct stat));

    if (!strcmp(path, "/")) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 1;
    } else {
        Link *link = path_to_Link(path);
        if (!link) {
            return -ENOENT;
        }
        struct timespec spec;
        spec.tv_sec = link->time;
        stbuf->st_mtim = spec;
        switch (link->type) {
            case LINK_DIR:
                stbuf->st_mode = S_IFDIR | 0755;
                stbuf->st_nlink = 1;
                break;
            case LINK_FILE:
                stbuf->st_mode = S_IFREG | 0444;
                stbuf->st_nlink = 1;
                stbuf->st_size = link->content_length;
                stbuf->st_blksize = 128*1024;
                stbuf->st_blocks = (link->content_length)/512;
                break;
            default:
                return -ENOENT;
        }
    }
    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();

    return res;
}

/** \brief read a file */
static int fs_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi)
{
//     size_t start = offset;
//     size_t end = start + size;
//     char range_str[64];
//     snprintf(range_str, sizeof(range_str), "%lu-%lu", start, end);
//     fprintf(stderr, "fs_read(%s, %s);\n", path, range_str);

    long received;
    if (CACHE_SYSTEM_INIT) {
        received = Cache_read((Cache *)fi->fh, buf, size, offset);
    } else {
        received = path_download(path, buf, size, offset);
    }
    return received;
}

/** \brief open a file indicated by the path */
static int fs_open(const char *path, struct fuse_file_info *fi)
{
    if (!path_to_Link(path)) {
        return -ENOENT;
    }

    if ((fi->flags & 3) != O_RDONLY) {
        return -EACCES;
    }

    if (CACHE_SYSTEM_INIT) {
        fi->fh = (uint64_t) Cache_open(path);
        if (!fi->fh) {
            return -ENOENT;
        }
    }

    fprintf(stderr, "fs_open(): %s\n", path);

    return 0;
}

/** \brief read the directory indicated by the path*/
static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t dir_add,
                      off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;

    Link *link;
    LinkTable *linktbl;

    if (!strcmp(path, "/")) {
        linktbl = ROOT_LINK_TBL;
    } else {
        linktbl = path_to_Link_LinkTable_new(path);
        if(!linktbl) {
            return -ENOENT;
        }
    }

    /* start adding the links */
    dir_add(buf, ".", NULL, 0);
    dir_add(buf, "..", NULL, 0);
    for (int i = 1; i < linktbl->num; i++) {
        link = linktbl->links[i];
        if (link->type != LINK_INVALID) {
            dir_add(buf, link->linkname, NULL, 0);
        }
    }

    return 0;
}

static struct fuse_operations fs_oper = {
    .getattr	= fs_getattr,
    .readdir	= fs_readdir,
    .open		= fs_open,
    .read		= fs_read,
    .init       = fs_init,
    .release    = fs_release
};

int fuse_local_init(int argc, char **argv)
{
    return fuse_main(argc, argv, &fs_oper, NULL);
}
