#include "fuse_local.h"

#include "link.h"
#include "log.h"

/*
 * must be included before including <fuse.h>
 */
#if defined(__APPLE__) && defined(__MACH__)
#define FUSE_DARWIN_ENABLE_EXTENSIONS 0
#endif

#define FUSE_USE_VERSION 30
#include <fuse.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

static void *fs_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    (void)conn;
    (void)cfg;
    return NULL;
}

/** \brief release an opened file */
static int fs_release(const char *path, struct fuse_file_info *fi)
{
    lprintf(info, "%s\n", path);
    (void)path;
    if (CACHE_SYSTEM_INIT && fi->fh) {
        Cache_close((Cache *)fi->fh);
    }
    return 0;
}

/** \brief return the attributes for a single file indicated by path */
static int fs_getattr(const char *path, struct stat *stbuf,
                      struct fuse_file_info *ffi_buf)
{
    (void)ffi_buf;
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
        struct timespec spec = {0};
        spec.tv_sec = link->time;
#if defined(__APPLE__) && defined(__MACH__)
        stbuf->st_mtime = spec.tv_sec;
        stbuf->st_atime = spec.tv_sec;
        stbuf->st_ctime = spec.tv_sec;
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
            stbuf->st_blksize = 128L * 1024L;
            stbuf->st_blocks = (link->content_length) / 512;
            break;
        default:
            LinkTable_unref(link->parent_table);
            return -ENOENT;
        }
        LinkTable_unref(link->parent_table);
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
        if (fi->fh) {
            received = Cache_read((Cache *)fi->fh, buf, size, offset);
        } else {
            /* zero-length file: fi->fh was set to 0 in fs_open; return EOF */
            received = 0;
        }
    } else {
        received = path_download(path, buf, size, offset);
    }
    return received;
}

/** \brief open a file indicated by the path */
static int fs_open(const char *path, struct fuse_file_info *fi)
{
    lprintf(info, "%s\n", path);
    Link *link = path_to_Link(path);
    if (!link) {
        return -ENOENT;
    }
    if ((fi->flags & O_RDWR) != O_RDONLY) {
        LinkTable_unref(link->parent_table);
        return -EROFS;
    }
    if (CACHE_SYSTEM_INIT) {
        if (link->content_length == 0) {
            fi->fh = 0; /* valid empty file: bypass cache creation */
            LinkTable_unref(link->parent_table);
            return 0;
        }
        fi->fh = (uint64_t)Cache_open(path);
        /*
         * The cache definitely cannot be opened for some reason.
         */
        if (!fi->fh) {
            lprintf(fatal, "Cache file creation failure for %s.\n", path);
        }
    }
    LinkTable_unref(link->parent_table);
    return 0;
}

static int fs_opendir(const char *path, struct fuse_file_info *fi)
{
    LinkTable *linktbl = path_to_LinkTable(path);
    if (!linktbl) {
        return -ENOENT;
    }
    fi->fh = (uint64_t)linktbl;
    return 0;
}

static int fs_releasedir(const char *path, struct fuse_file_info *fi)
{
    LinkTable *linktbl = (LinkTable *)fi->fh;
    if (linktbl) {
        if (strcmp(path, "/") != 0) {
            LinkTable_mark_orphaned(linktbl);
        }
        LinkTable_unref(linktbl);
    }
    return 0;
}

/**
 * \brief read the directory indicated by the path
 */
static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t dir_add,
                      off_t offset, struct fuse_file_info *fi,
                      enum fuse_readdir_flags fr_flags)
{
    (void)path;
    (void)offset;
    (void)fr_flags;
    LinkTable *linktbl = (LinkTable *)fi->fh;

    if (!linktbl) {
        return -ENOENT;
    }


    /*
     * start adding the links
     */
    dir_add(buf, ".", NULL, 0, 0);
    dir_add(buf, "..", NULL, 0, 0);
    /* We skip the head link */
    for (int i = 1; i < linktbl->size; i++) {
        Link *link = linktbl->links[i];
        if (link->type != LINK_INVALID) {
            dir_add(buf, link->linkname, NULL, 0, 0);
        }
    }

    return 0;
}

static struct fuse_operations fs_oper = {.getattr = fs_getattr,
                                         .opendir = fs_opendir,
                                         .readdir = fs_readdir,
                                         .releasedir = fs_releasedir,
                                         .open = fs_open,
                                         .read = fs_read,
                                         .init = fs_init,
                                         .release = fs_release};

int fuse_local_init(int argc, char **argv)
{
    return fuse_main(argc, argv, &fs_oper, NULL);
}
