#include "fuse_local.h"

#include "cache.h"

/* must be included before including <fuse.h> */
#define FUSE_USE_VERSION 26
#include <fuse.h>

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
    (void) path;
    if (CACHE_SYSTEM_INIT) {
        Cache_close((Cache *)fi->fh);
    }
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
#if defined(__APPLE__) && defined(__MACH__)
        stbuf->st_mtimespec = spec;
#else
        stbuf->st_mtim = spec;
#endif
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
    Link *link = path_to_Link(path);
    if (!link) {
        return -ENOENT;
    }
    if ((fi->flags & 3) != O_RDONLY) {
        return -EACCES;
    }
    if (CACHE_SYSTEM_INIT) {
        fi->fh = (uint64_t) Cache_open(path);
        if (!fi->fh) {
            /*
             * The link clearly exists, the cache cannot be opened, attempt
             * cache creation
             */
            Cache_delete(path);
            Cache_create(path);
            fi->fh = (uint64_t) Cache_open(path);
            /*
             * The cache definitely cannot be opened for some reason.
             */
            if (!fi->fh) {
                return -ENOENT;
            }
        }
    }
    return 0;
}

/**
 * \brief read the directory indicated by the path
 * \note
 *  - releasedir() is not implemented, because I don't see why anybody want
 * the LinkTables to be evicted from the memory during the runtime of this
 * program. If you want to evict LinkTables, just unmount the filesystem.
 *  - There is no real need to associate the LinkTable with the fi of each
 * directory data structure. If you want a deep level directory, you need to
 * generate the LinkTables for previous level directories. We might
 * as well maintain our own tree structure.
 */
static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t dir_add,
                      off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;

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
        Link *link = linktbl->links[i];
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
