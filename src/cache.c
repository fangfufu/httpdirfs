#include "cache.h"

#include "config.h"
#include "log.h"
#include "memcache.h"
#include "util.h"
#include <curl/curl.h>

#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * ---------------- External variables -----------------------
 */
int CACHE_SYSTEM_INIT = 0;
char *META_DIR;

/*
 * ----------------- Static variables -----------------------
 */

/**
 * \brief Cache file locking
 * \details Ensure cache opening and cache closing is an atomic operation
 */
static pthread_mutex_t cf_lock;

/**
 * \brief The data directory
 */
static char *DATA_DIR;


char *CacheSystem_get_cache_dir(void)
{
    if (CONFIG.cache_dir) {
        return CONFIG.cache_dir;
    }

    const char *default_cache_subdir = "/.cache";
    char *cache_dir = NULL;

    const char *xdg_cache_home = getenv("XDG_CACHE_HOME");
    if (xdg_cache_home) {
        cache_dir = STRNDUP(xdg_cache_home, PATH_MAX);
    } else {
        const char *user_home = getenv("HOME");
        if (user_home) {
            cache_dir = path_append(user_home, default_cache_subdir);
        } else {
            lprintf(warning, "$HOME is unset\n");
            /*
             * XDG_CACHE_HOME and HOME already are full paths. Not relying
             * on environment PWD since it too may be undefined.
             */
            char *cur_dir = REALPATH("./", NULL);
            if (cur_dir) {
                cache_dir = path_append(cur_dir, default_cache_subdir);
                FREE(cur_dir);
            } else {
                lprintf(fatal, "Could not create cache directory\n");
            }
        }
    }
    return cache_dir;
}

/**
 * \brief Calculate cache system directory path
 */
static char *CacheSystem_calc_dir(const char *url)
{
    char *cache_home = CacheSystem_get_cache_dir();

    if (mkdir(cache_home, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
        && (errno != EEXIST)) {
        lprintf(fatal, "mkdir(): %s\n", strerror(errno));
    }
    char *cache_dir_root = path_append(cache_home, "/httpdirfs/");
    if (mkdir(cache_dir_root, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
        && (errno != EEXIST)) {
        lprintf(fatal, "mkdir(): %s\n", strerror(errno));
    }

    char *fn = path_append(cache_dir_root, "/CACHEDIR.TAG");
    FILE *fp = fopen(fn, "w");
    if (fp) {
        fprintf(fp, "Signature: 8a477f597d28d172789f06886806bc55\n\
# This file is a cache directory tag created by httpdirfs.\n\
# For information about cache directory tags, see:\n\
#	http://www.brynosaurus.com/cachedir/\n");
    } else {
        lprintf(fatal, "fopen(%s): %s", fn, strerror(errno));
    }
    if (ferror(fp)) {
        lprintf(fatal, "fwrite(): encountered error!\n");
    }
    if (fclose(fp)) {
        lprintf(fatal, "fclose(%s): %s\n", fn, strerror(errno));
    }
    CURL *c = curl_easy_init();
    char *escaped_url = curl_easy_escape(c, url, 0);
    char *full_path = path_append(cache_dir_root, escaped_url);
    if (mkdir(full_path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
        && (errno != EEXIST)) {
        lprintf(fatal, "mkdir(): %s\n", strerror(errno));
    }
    FREE(fn);
    FREE(cache_home);
    FREE(cache_dir_root);
    curl_free(escaped_url);
    curl_easy_cleanup(c);
    return full_path;
}

void CacheSystem_init(const char *path, int url_supplied)
{
    lprintf(cache_lock_debug, "thread %lx: initialise cf_lock;\n",
            (unsigned long)pthread_self());
    PTHREAD_MUTEX_INIT(&cf_lock, NULL);

    if (url_supplied) {
        path = CacheSystem_calc_dir(path);
    }

    META_DIR = path_append(path, "meta/");
    DATA_DIR = path_append(path, "data/");
    /*
     * Check if directories exist, if not, create them
     */
    if (mkdir(META_DIR, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
        && (errno != EEXIST)) {
        lprintf(fatal, "mkdir(): %s\n", strerror(errno));
    }

    if (mkdir(DATA_DIR, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
        && (errno != EEXIST)) {
        lprintf(fatal, "mkdir(): %s\n", strerror(errno));
    }

    if (CONFIG.mode == SONIC) {
        char *sonic_path;
        /*
         * Create "rest" sub-directory for META_DIR
         */
        sonic_path = path_append(META_DIR, "rest/");
        if (mkdir(sonic_path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
            && (errno != EEXIST)) {
            lprintf(fatal, "mkdir(): %s\n", strerror(errno));
        }
        FREE(sonic_path);

        /*
         * Create "rest" sub-directory for DATA_DIR
         */
        sonic_path = path_append(DATA_DIR, "rest/");
        if (mkdir(sonic_path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
            && (errno != EEXIST)) {
            lprintf(fatal, "mkdir(): %s\n", strerror(errno));
        }
        FREE(sonic_path);
    }

    CACHE_SYSTEM_INIT = 1;
}

void CacheSystem_cleanup(void)
{
    if (CACHE_SYSTEM_INIT) {
        FREE(META_DIR);
        FREE(DATA_DIR);
        PTHREAD_MUTEX_DESTROY(&cf_lock);
        CACHE_SYSTEM_INIT = 0;
    }
}

static int ntfw_cb(const char *fpath, const struct stat *sb, int typeflag,
                   struct FTW *ftwbuf)
{
    (void)sb;
    (void)typeflag;
    (void)ftwbuf;
    return remove(fpath);
}

void CacheSystem_clear(void)
{
    char *cache_home = CacheSystem_get_cache_dir();
    const char *cache_del;
    if (CONFIG.cache_dir) {
        cache_del = cache_home;
    } else {
        cache_del = path_append(cache_home, "/httpdirfs/");
    }
    nftw(cache_del, ntfw_cb, 64, FTW_DEPTH | FTW_PHYS | FTW_MOUNT);
    FREE(cache_home);
    exit(EXIT_SUCCESS);
}

/**
 * \brief read a metadata file
 * \return 0 on success, errno on error.
 */
static int Meta_read(Cache *cf)
{
    FILE *fp = cf->mfp;

    if (!fp) {
        /*
         * The metadata file does not exist
         */
        lprintf(error, "fopen(): %s\n", strerror(errno));
        return EIO;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        lprintf(error, "fseek(): %s\n", strerror(errno));
        return EIO;
    }

    if (!cf->link) {
        lprintf(error, "cf->link is NULL in Meta_read\n");
        return EINVAL;
    }

    long disk_time;
    off_t disk_content_length;

    if (1 != fread(&disk_time, sizeof(long), 1, fp)
        || 1 != fread(&disk_content_length, sizeof(off_t), 1, fp)
        || 1 != fread(&cf->blksz, sizeof(int), 1, fp)
        || 1 != fread(&cf->segbc, sizeof(long), 1, fp) || ferror(fp)) {
        lprintf(error, "error reading core metadata %s!\n", cf->path);
        return EIO;
    }

    /*
     * We do not support zero-byte files in the on-disk cache files.
     * Both disk_content_length and cf->segbc must be strictly positive.
     */
    if (disk_content_length <= 0 || cf->blksz <= 0 || cf->segbc <= 0) {
        lprintf(error,
                "corruption: content_length: %jd, blksz: %d, segbc: %jd\n",
                (intmax_t)disk_content_length, cf->blksz, (intmax_t)cf->segbc);
        return EBADMSG;
    }

    if (cf->blksz != CONFIG.data_blksz) {
        lprintf(warning, "Warning: cf->blksz != CONFIG.data_blksz\n");
    }

    if (disk_content_length > INT64_MAX - cf->blksz) {
        lprintf(error, "Error: segbc upper bound overflow\n");
        return EBADMSG;
    }

    /* Verify cached metadata matches the live Link metadata */
    if (disk_time != cf->link->time) {
        lprintf(warning, "outdated cache file: %s (disk: %ld, link: %ld)\n",
                cf->path, disk_time, cf->link->time);
        return EBADMSG;
    }

    if ((uintmax_t)disk_content_length != (uintmax_t)cf->link->content_length) {
        lprintf(warning, "cache size mismatch: %s (disk: %jd, link: %zu)\n",
                cf->path, (intmax_t)disk_content_length,
                cf->link->content_length);
        return EBADMSG;
    }

    off_t max_segbc = disk_content_length / cf->blksz;

    if ((disk_content_length % cf->blksz) != 0) {
        max_segbc += 1;
    }

    if (max_segbc > INT_MAX) {
        max_segbc = INT_MAX;
    }

    if (cf->segbc != max_segbc) {
        lprintf(error, "Error: invalid segbc size: %ld (expected: %ld)\n",
                cf->segbc, (long)max_segbc);
        return EBADMSG;
    }

    /*
     * Allocate memory for all segments, and read them in
     */
    cf->seg = CALLOC(cf->segbc, sizeof(Seg));
    long nmemb = fread(cf->seg, sizeof(Seg), cf->segbc, fp);

    /*
     * We shouldn't have gone past the end of the file
     */
    if (feof(fp)) {
        /*
         * reached EOF
         */
        lprintf(error, "attempted to read past the end of the file!\n");
        return EBADMSG;
    }

    /*
     * Error checking for fread
     */
    if (ferror(fp)) {
        lprintf(error, "error reading bitmap!\n");
        return EIO;
    }

    /*
     * Check for inconsistent metadata file
     */
    if (nmemb != cf->segbc) {
        lprintf(error, "corrupted metadata!\n");
        return EBADMSG;
    }

    return 0;
}

/**
 * \brief write a metadata file
 * \return
 *  - -1 on error,
 *  - 0 on success
 */
static int Meta_write(Cache *cf)
{
    FILE *fp = cf->mfp;

    if (!fp) {
        /*
         * Cannot create the metadata file
         */
        lprintf(error, "fopen(): %s\n", strerror(errno));
        return -1;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        lprintf(error, "fseek(): %s\n", strerror(errno));
        return -1;
    }

    if (!cf->link) {
        lprintf(error, "cf->link is NULL in Meta_write\n");
        return -1;
    }

    /*
     * We support zero-byte files, in which case both write_content_length and
     * cf->segbc must be zero. If write_content_length is positive, cf->segbc
     * must be positive.
     */
    off_t write_content_length = (off_t)cf->link->content_length;
    long expected_segbc = 0;
    if (cf->blksz > 0 && write_content_length >= 0) {
        expected_segbc = write_content_length / cf->blksz;
        if (write_content_length % cf->blksz != 0) {
            expected_segbc += 1;
        }
        if (expected_segbc > INT_MAX) {
            expected_segbc = INT_MAX;
        }
    }
    if (write_content_length < 0
        || (size_t)write_content_length != cf->link->content_length
        || cf->blksz <= 0 || cf->segbc != expected_segbc) {
        lprintf(error,
                "invalid metadata for write: content_length: %jd, blksz: %d, "
                "segbc: %ld (expected: %ld)\n",
                (intmax_t)write_content_length, cf->blksz, cf->segbc,
                expected_segbc);
        return -1;
    }

    fwrite(&cf->link->time, sizeof(long), 1, fp);
    fwrite(&write_content_length, sizeof(off_t), 1, fp);
    fwrite(&cf->blksz, sizeof(int), 1, fp);
    fwrite(&cf->segbc, sizeof(long), 1, fp);
    fwrite(cf->seg, sizeof(Seg), cf->segbc, fp);

    /*
     * Error checking for fwrite
     */
    if (ferror(fp)) {
        lprintf(error, "fwrite(): encountered error!\n");
        return -1;
    }

    return 0;
}

/**
 * \brief create a data file
 * \details We use sparse creation here
 * \return exit on failure
 */
static void Data_create(Cache *cf)
{
    int fd;
    int mode;

    mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    char *datafn = path_append(DATA_DIR, cf->path);
    fd = open(datafn, O_WRONLY | O_CREAT, mode);
    FREE(datafn);
    if (fd == -1) {
        lprintf(fatal, "open(): %s\n", strerror(errno));
    }
    if (!cf->link) {
        lprintf(fatal, "cf->link is NULL in Data_create\n");
    }
    off_t truncate_size = (off_t)cf->link->content_length;
    if (truncate_size < 0
        || (size_t)truncate_size != cf->link->content_length) {
        lprintf(fatal, "File size too large for system off_t: %zu\n",
                cf->link->content_length);
    }
    if (ftruncate(fd, truncate_size)) {
        lprintf(warning, "ftruncate(): %s\n", strerror(errno));
    }
    if (close(fd)) {
        lprintf(fatal, "close:(): %s\n", strerror(errno));
    }
}

/**
 * \brief obtain the data file size
 * \return file size on success, -1 on error
 */
static off_t Data_size(const char *fn)
{
    char *datafn = path_append(DATA_DIR, fn);
    struct stat st;
    int s = stat(datafn, &st);
    FREE(datafn);
    if (!s) {
        return st.st_size;
    }
    lprintf(error, "stat(): %s\n", strerror(errno));
    return -1;
}

/**
 * \brief read a data file
 * \param[in] cf the pointer to the cache in-memory data structure
 * \param[out] buf the output buffer
 * \param[in] len the length of the segment
 * \param[in] offset the offset of the segment
 * \return
 *  - negative values on error,
 *  - otherwise, the number of bytes read.
 */
static long Data_read(Cache *cf, uint8_t *buf, off_t len, off_t offset)
{
    if (!cf->link) {
        lprintf(error, "cf->link is NULL in Data_read\n");
        return -EINVAL;
    }

    if (len == 0) {
        lprintf(error, "requested to read 0 byte!\n");
        return -EINVAL;
    }

    lprintf(cache_lock_debug, "thread %lx: locking seek_lock;\n",
            (unsigned long)pthread_self());
    PTHREAD_MUTEX_LOCK(&cf->seek_lock);

    long byte_read = 0;

    if (offset < 0 || offset >= (off_t)cf->link->content_length) {
        goto end;
    } else if (len > (off_t)cf->link->content_length - offset) {
        len = (off_t)cf->link->content_length - offset;
    }

    /*
     * Seek to the right location
     */
    if (fseeko(cf->dfp, offset, SEEK_SET)) {
        /*
         * fseeko failed
         */
        lprintf(error, "fseeko(): %s\n", strerror(errno));
        byte_read = -EIO;
        goto end;
    }

    byte_read = fread(buf, sizeof(uint8_t), len, cf->dfp);
    if (byte_read != len) {
        if (feof(cf->dfp)) {
            /*
             * reached EOF
             */
            lprintf(error, "fread(): reached the end of the file!\n");
        }
        if (ferror(cf->dfp)) {
            /*
             * filesystem error
             */
            lprintf(error, "fread(): encountered error!\n");
        }
    }

end:

    lprintf(cache_lock_debug, "thread %lx: unlocking seek_lock;\n",
            (unsigned long)pthread_self());
    PTHREAD_MUTEX_UNLOCK(&cf->seek_lock);
    return byte_read;
}

/**
 * \brief write to a data file
 * \param[in] cf the pointer to the cache in-memory data structure
 * \param[in] buf the input buffer
 * \param[in] len the length of the segment
 * \param[in] offset the offset of the segment
 * \return
 *  - -1 when the data file does not exist
 *  - otherwise, the number of bytes written.
 */
static long Data_write(Cache *cf, const uint8_t *buf, off_t len, off_t offset)
{
    if (len == 0) {
        /*
         * We should permit empty files
         */
        return 0;
    }

    lprintf(cache_lock_debug, "thread %lx: locking seek_lock;\n",
            (unsigned long)pthread_self());
    PTHREAD_MUTEX_LOCK(&cf->seek_lock);

    long byte_written = 0;

    if (fseeko(cf->dfp, offset, SEEK_SET)) {
        /*
         * fseeko failed
         */
        lprintf(error, "fseeko(): %s\n", strerror(errno));
        byte_written = -EIO;
        goto end;
    }

    byte_written = fwrite(buf, sizeof(uint8_t), len, cf->dfp);

    if (byte_written != len) {
        lprintf(error, "fwrite(): requested %ld, returned %ld!\n", len,
                byte_written);
    }

    if (ferror(cf->dfp)) {
        /*
         * filesystem error
         */
        lprintf(error, "fwrite(): encountered error!\n");
    }

end:
    lprintf(cache_lock_debug, "thread %lx: unlocking seek_lock;\n",
            (unsigned long)pthread_self());
    PTHREAD_MUTEX_UNLOCK(&cf->seek_lock);
    return byte_written;
}

int CacheDir_create(const char *dirn)
{
    char *metadirn = path_append(META_DIR, dirn);
    char *datadirn = path_append(DATA_DIR, dirn);
    int res = 0;

    if (mkdir(metadirn, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0
        && errno != EEXIST) {
        lprintf(fatal, "mkdir(%s): %s\n", metadirn, strerror(errno));
        res |= 1;
    }

    if (mkdir(datadirn, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0
        && errno != EEXIST) {
        lprintf(fatal, "mkdir(%s): %s\n", datadirn, strerror(errno));
        res |= 2;
    }
    FREE(datadirn);
    FREE(metadirn);
    return res;
}
ActiveDownload *ActiveDownload_find(Cache *cf, off_t offset)
{
    ActiveDownload *ad = cf->active_dls;
    while (ad) {
        if (ad->offset == offset) {
            return ad;
        }
        ad = ad->next;
    }
    return NULL;
}

/**
 * \brief Allocates and prepends a new ActiveDownload tracker to the list.
 * \param[in] cf The cache instance.
 * \param[in] offset The offset to track.
 * \note Must be called while holding cf->dl_lock.
 */
static void ActiveDownload_add(Cache *cf, off_t offset)
{
    ActiveDownload *ad = CALLOC(1, sizeof(ActiveDownload));
    ad->offset = offset;
    ad->ts = NULL;
    ad->next = cf->active_dls;
    cf->active_dls = ad;
}

/**
 * \brief Removes and frees the ActiveDownload tracker for the given offset.
 * \param[in] cf The cache instance.
 * \param[in] offset The offset to untrack.
 * \note Must be called while holding cf->dl_lock.
 */
static void ActiveDownload_remove(Cache *cf, off_t offset)
{
    ActiveDownload **curr = &cf->active_dls;
    while (*curr) {
        if ((*curr)->offset == offset) {
            ActiveDownload *temp = *curr;
            *curr = (*curr)->next;
            FREE(temp);
            return;
        }
        curr = &(*curr)->next;
    }
}

/**
 * \brief Allocate a new cache data structure
 */
static Cache *Cache_alloc(void)
{
    Cache *cf = CALLOC(1, sizeof(Cache));
    PTHREAD_MUTEX_INIT(&cf->seek_lock, NULL);
    PTHREAD_MUTEX_INIT(&cf->w_lock, NULL);
    PTHREAD_MUTEX_INIT(&cf->dl_lock, NULL);
    PTHREAD_COND_INIT(&cf->dl_cond, NULL);
    cf->active_dls = NULL;
    cf->cache_opened = 1;
    SEM_INIT(&cf->bgt_sem, 0, 1);
    return cf;
}

/**
 * \brief free a cache data structure
 */
static void Cache_free(Cache *cf)
{
    PTHREAD_MUTEX_DESTROY(&cf->seek_lock);
    PTHREAD_MUTEX_DESTROY(&cf->w_lock);
    PTHREAD_MUTEX_DESTROY(&cf->dl_lock);
    PTHREAD_COND_DESTROY(&cf->dl_cond);
    SEM_DESTROY(&cf->bgt_sem);

    if (cf->path) {
        FREE(cf->path);
    }

    if (cf->seg) {
        FREE(cf->seg);
    }

    ActiveDownload *ad = cf->active_dls;
    while (ad) {
        ActiveDownload *next = ad->next;
        FREE(ad);
        ad = next;
    }

    FREE(cf);
}

/**
 * \brief Check if both metadata and data file exist, otherwise perform cleanup.
 * \details
 * This function checks if both metadata file and the data file exist. If that
 * is not the case, clean up is performed - the existing unpaired metadata file
 * or data file is deleted.
 * \return
 *  -   0, if both metadata and cache file exist
 *  -   -1, otherwise
 */
static int Cache_exist(const char *fn)
{
    char *metafn = path_append(META_DIR, fn);
    char *datafn = path_append(DATA_DIR, fn);
    /*
     * access() returns 0 on success
     */
    int no_meta = access(metafn, F_OK);
    int no_data = access(datafn, F_OK);

    if (no_meta ^ no_data) {
        lprintf(warning, "Cache file partially missing.\n");
        if (no_meta) {
            if (unlink(datafn)) {
                lprintf(fatal, "unlink(): %s\n", strerror(errno));
            }
        }
        if (no_data) {
            if (unlink(metafn)) {
                lprintf(fatal, "unlink(): %s\n", strerror(errno));
            }
        }
    }

    FREE(metafn);
    FREE(datafn);

    return no_meta | no_data;
}

/**
 * \brief delete a cache file set
 */
void Cache_delete(const char *fn)
{
    Link *link = path_to_Link(fn);
    if (CONFIG.mode == SONIC) {
        if (!link) {
            return;
        }
        fn = link->sonic.id;
    }

    char *metafn = path_append(META_DIR, fn);
    char *datafn = path_append(DATA_DIR, fn);
    if (!access(metafn, F_OK) && unlink(metafn)) {
        lprintf(error, "unlink(): %s\n", strerror(errno));
    }

    if (!access(datafn, F_OK) && unlink(datafn)) {
        lprintf(error, "unlink(): %s\n", strerror(errno));
    }
    FREE(metafn);
    FREE(datafn);
    if (link) {
        LinkTable_unref(link->parent_table);
    }
}

/**
 * \brief Open the data file of a cache data set
 * \return
 *  -   0 on success
 *  -   -1 on failure, with appropriate errno set.
 */
static int Data_open(Cache *cf)
{
    char *datafn = path_append(DATA_DIR, cf->path);
    cf->dfp = fopen(datafn, "r+");
    if (!cf->dfp) {
        /*
         * Failed to open the data file
         */
        lprintf(error, "fopen(%s): %s\n", datafn, strerror(errno));
        FREE(datafn);
        return -1;
    }
    FREE(datafn);
    return 0;
}

/**
 * \brief Open a metafile
 * \return
 *  -   0 on success
 *  -   -1 on failure, with appropriate errno set.
 */
static int Meta_open(Cache *cf)
{
    char *metafn = path_append(META_DIR, cf->path);
    cf->mfp = fopen(metafn, "r+");
    if (!cf->mfp) {
        /*
         * Failed to open the data file
         */
        lprintf(error, "fopen(%s): %s\n", metafn, strerror(errno));
        FREE(metafn);
        return -1;
    }
    FREE(metafn);
    return 0;
}

/**
 * \brief Create a metafile
 * \return exit on error
 */
static void Meta_create(Cache *cf)
{
    char *metafn = path_append(META_DIR, cf->path);
    cf->mfp = fopen(metafn, "w");
    if (!cf->mfp) {
        /*
         * Failed to open the data file
         */
        lprintf(fatal, "fopen(%s): %s\n", metafn, strerror(errno));
    }
    if (fclose(cf->mfp)) {
        lprintf(error, "cannot close metadata after creation: %s.\n",
                strerror(errno));
    }
    FREE(metafn);
}

int Cache_create(const char *path)
{
    Link *this_link = path_to_Link(path);
    if (!this_link) {
        return 1;
    }

    char *fn;

    if (CONFIG.mode == NORMAL) {
        fn = curl_easy_unescape(NULL, this_link->f_url + ROOT_LINK_OFFSET, 0,
                                NULL);
    } else if (CONFIG.mode == SINGLE) {
        fn = curl_easy_unescape(NULL, this_link->linkname, 0, NULL);
    } else if (CONFIG.mode == SONIC) {
        fn = this_link->sonic.id;
    } else {
        lprintf(fatal, "Invalid CONFIG.mode\n");
    }
    Cache *cf = Cache_alloc();
    cf->path = STRNDUP(fn, PATH_MAX);
    cf->link = this_link;
    cf->blksz = CONFIG.data_blksz;
    cf->segbc = this_link->content_length / cf->blksz;
    if (this_link->content_length % cf->blksz != 0) {
        cf->segbc += 1;
    }
    if (cf->segbc > INT_MAX) {
        cf->segbc = INT_MAX;
    }
    cf->seg = CALLOC(cf->segbc, sizeof(Seg));

    Meta_create(cf);

    if (Meta_open(cf)) {
        lprintf(error, "cannot open metadata file, %s.\n", fn);
        Cache_free(cf);
    }

    if (Meta_write(cf)) {
        lprintf(error, "Meta_write() failed!\n");
    }

    if (fclose(cf->mfp)) {
        lprintf(error, "cannot close metadata after write, %s.\n",
                strerror(errno));
    }

    Data_create(cf);

    lprintf(cache_lock_debug, "Flushing cache file for %s after creating.\n",
            fn);
    Cache_free(cf);

    int res = Cache_exist(fn);

    if (res) {
        lprintf(fatal, "Cache file creation failed for %s\n", path);
    }

    if (CONFIG.mode == NORMAL || CONFIG.mode == SONIC) {
        curl_free(fn);
    }

    if (this_link) {
        LinkTable_unref(this_link->parent_table);
    }

    return res;
}

Cache *Cache_open(const char *fn)
{
    /*
     * Obtain the link structure memory pointer
     */
    Link *link = path_to_Link(fn);
    if (!link) {
        /*
         * There is no associated link to the path
         */
        return NULL;
    }

    lprintf(cache_lock_debug, "thread %lx: locking cf_lock;\n",
            (unsigned long)pthread_self());
    PTHREAD_MUTEX_LOCK(&cf_lock);

    if (link->cache_ptr) {
        link->cache_ptr->cache_opened++;
        lprintf(cache_lock_debug, "thread %lx: unlocking cf_lock;\n",
                (unsigned long)pthread_self());
        PTHREAD_MUTEX_UNLOCK(&cf_lock);
        LinkTable_unref(link->parent_table);
        return link->cache_ptr;
    }

    const char *actual_fn = fn;
    if (CONFIG.mode == SONIC) {
        actual_fn = link->sonic.id;
    }

    if (link->content_length == 0) {
        Cache_delete(fn);
        Cache *cf = Cache_alloc();
        cf->path = STRNDUP(actual_fn, PATH_MAX);
        cf->link = link;
        cf->blksz = CONFIG.data_blksz;
        cf->segbc = 0;
        cf->seg = NULL;
        cf->mfp = NULL;
        cf->dfp = NULL;

        link->cache_ptr = cf;

        lprintf(cache_lock_debug,
                "thread %lx: unlocking cf_lock; (zero-length file shortcut)\n",
                (unsigned long)pthread_self());
        PTHREAD_MUTEX_UNLOCK(&cf_lock);
        return cf;
    }

    // Try up to 2 times. If opening fails on the first attempt (outdated,
    // corrupt, etc.), we delete the cache and try creating and opening a fresh
    // one.
    for (int attempt = 0; attempt < 2; attempt++) {
        if (Cache_exist(actual_fn) != 0) {
            Cache_delete(fn);
            if (Cache_create(fn) != 0) {
                lprintf(cache_lock_debug, "thread %lx: unlocking cf_lock;\n",
                        (unsigned long)pthread_self());
                PTHREAD_MUTEX_UNLOCK(&cf_lock);
                LinkTable_unref(link->parent_table);
                return NULL;
            }
        }

        /*
         * Create the cache in-memory data structure
         */
        Cache *cf = Cache_alloc();

        cf->path = STRNDUP(actual_fn, PATH_MAX);

        /*
         * Associate the cache structure with a link
         */
        cf->link = link;

        int ok = 1;
        if (Meta_open(cf)) {
            lprintf(error, "cannot open metadata file %s.\n", actual_fn);
            ok = 0;
        } else if (Meta_read(cf)) {
            lprintf(error, "metadata error: %s.\n", actual_fn);
            ok = 0;
        } else {
            off_t d_size = Data_size(actual_fn);
            if (d_size < 0) {
                lprintf(error, "cannot stat data file %s.\n", actual_fn);
                ok = 0;
            } else if ((uintmax_t)cf->link->content_length
                       > (uintmax_t)d_size) {
                lprintf(error,
                        "metadata inconsistency %s, "
                        "cf->link->content_length: %jd, Data_size(fn): %jd.\n",
                        actual_fn, (intmax_t)cf->link->content_length,
                        (intmax_t)d_size);
                ok = 0;
            } else if (Data_open(cf)) {
                lprintf(error, "cannot open data file %s.\n", actual_fn);
                ok = 0;
            }
        }

        if (ok) {
            /*
             * Yup, we just created a circular loop. ;)
             */
            cf->link->cache_ptr = cf;

            lprintf(cache_lock_debug, "thread %lx: unlocking cf_lock;\n",
                    (unsigned long)pthread_self());
            PTHREAD_MUTEX_UNLOCK(&cf_lock);
            return cf;
        }

        // Clean up opened resources before retry
        if (cf->mfp) {
            fclose(cf->mfp);
            cf->mfp = NULL;
        }
        if (cf->dfp) {
            fclose(cf->dfp);
            cf->dfp = NULL;
        }
        Cache_free(cf);
        Cache_delete(fn);
    }

    lprintf(cache_lock_debug, "thread %lx: unlocking cf_lock;\n",
            (unsigned long)pthread_self());
    PTHREAD_MUTEX_UNLOCK(&cf_lock);
    LinkTable_unref(link->parent_table);
    return NULL;
}

void Cache_close(Cache *cf)
{
    lprintf(cache_lock_debug, "thread %lx: locking cf_lock: %s\n",
            (unsigned long)pthread_self(), cf->path);
    PTHREAD_MUTEX_LOCK(&cf_lock);

    cf->cache_opened--;

    if (cf->cache_opened > 0) {
        lprintf(cache_lock_debug,
                "thread %lx: unlocking cf_lock: %s, cache_opened: %d\n",
                (unsigned long)pthread_self(), cf->path, cf->cache_opened);
        PTHREAD_MUTEX_UNLOCK(&cf_lock);
        return;
    }

    /*
     * Wait for any background download to finish before closing. If we don't
     * wait, Cache_free() might be called while Cache_bgdl() is still
     * running. This will cause a use-after-free error.
     */
    lprintf(cache_lock_debug,
            "thread %lx: waiting for background download to finish for %s\n",
            (unsigned long)pthread_self(), cf->path);
    SEM_WAIT(&cf->bgt_sem);

    if (cf->mfp && Meta_write(cf)) {
        lprintf(error, "Meta_write() error.");
    }

    if (cf->mfp && fclose(cf->mfp)) {
        lprintf(error, "cannot close metadata: %s.\n", strerror(errno));
    }

    if (cf->dfp && fclose(cf->dfp)) {
        lprintf(error, "cannot close data file %s.\n", strerror(errno));
    }

    Link *link = cf->link;
    link->cache_ptr = NULL;

    lprintf(cache_lock_debug,
            "thread %lx: unlocking cf_lock, cache closed: %s\n",
            (unsigned long)pthread_self(), cf->path);
    Cache_free(cf);
    LinkTable_unref(link->parent_table);
    PTHREAD_MUTEX_UNLOCK(&cf_lock);
}

/**
 * \brief Check if a segment exists.
 * \return 1 if the segment exists
 */
static int Seg_exist(Cache *cf, off_t offset)
{
    if (cf->segbc <= 0) {
        return 0;
    }
    off_t byte = offset / cf->blksz;
    if (byte < 0 || byte >= cf->segbc) {
        return 0;
    }
    return cf->seg[byte];
}

/**
 * \brief Set the existence of a segment
 * \param[in] cf the cache in-memory data structure
 * \param[in] offset the starting position of the segment.
 * \param[in] i 1 for exist, 0 for doesn't exist
 * \note Call this after downloading a segment.
 */
static void Seg_set(Cache *cf, off_t offset, int i)
{
    if (cf->segbc <= 0) {
        return;
    }
    off_t byte = offset / cf->blksz;
    if (byte < 0 || byte >= cf->segbc) {
        return;
    }
    cf->seg[byte] = i;
}

/**
 * \brief Arguments passed to the background download thread.
 */
typedef struct BgdlArg {
    Cache *cf;       /**< The cache instance. */
    off_t dl_offset; /**< The segment offset to download. */
} BgdlArg;

/**
 * \brief Background download function
 * \details If we are requesting the data from the second half of the current
 * segment, we can spawn a pthread using this function to download the next
 * segment.
 * \param[in] arg A pointer to a BgdlArg structure.
 */
static void *Cache_bgdl(void *arg)
{
    BgdlArg *bg_arg = (BgdlArg *)arg;
    Cache *cf = bg_arg->cf;
    off_t dl_offset = bg_arg->dl_offset;
    FREE(bg_arg);

    uint8_t *recv_buf = CALLOC(cf->blksz, sizeof(uint8_t));
    long recv
        = Link_download(cf->link, (char *)recv_buf, cf->blksz, dl_offset, cf);
    if (recv < 0) {
        lprintf(error,
                "thread %lx received %ld bytes, "
                "which doesn't make sense\n",
                (unsigned long)pthread_self(), recv);
        FREE(recv_buf);
        PTHREAD_MUTEX_LOCK(&cf->dl_lock);
        ActiveDownload_remove(cf, dl_offset);
        PTHREAD_COND_BROADCAST(&cf->dl_cond);
        PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
        SEM_POST(&cf->bgt_sem);
        pthread_exit(NULL);
    }

    PTHREAD_MUTEX_LOCK(&cf->w_lock);
    if ((recv == cf->blksz)
        || ((uintmax_t)dl_offset
            == (cf->link->content_length / (size_t)cf->blksz
                * (size_t)cf->blksz))) {
        if (Data_write(cf, recv_buf, recv, dl_offset) == recv) {
            Seg_set(cf, dl_offset, 1);
        }
    } else {
        lprintf(error,
                "received %ld rather than %d, possible network "
                "error.\n",
                recv, cf->blksz);
    }
    PTHREAD_MUTEX_UNLOCK(&cf->w_lock);

    FREE(recv_buf);

    PTHREAD_MUTEX_LOCK(&cf->dl_lock);
    ActiveDownload_remove(cf, dl_offset);
    PTHREAD_COND_BROADCAST(&cf->dl_cond);
    PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);

    SEM_POST(&cf->bgt_sem);

    pthread_exit(NULL);
}

/**
 * \brief Spawns a background thread to download the next segment.
 * \param[in] cf The cache instance.
 * \param[in] dl_offset The offset of the segment to download.
 */
static void Cache_bgdl_launcher(Cache *cf, off_t dl_offset)
{
    pthread_t thread;
    pthread_attr_t attr;

    if (pthread_attr_init(&attr)) {
        lprintf(fatal, "pthread_attr_init():%d, %s\n", errno, strerror(errno));
    }

    if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) {
        lprintf(fatal, "pthread_attr_setdetachstate():%d, %s\n", errno,
                strerror(errno));
    }

    BgdlArg *arg = CALLOC(1, sizeof(BgdlArg));
    arg->cf = cf;
    arg->dl_offset = dl_offset;

    if (pthread_create(&thread, &attr, Cache_bgdl, arg)) {
        FREE(arg);
        lprintf(fatal, "pthread_create(): %d, %s\n", errno, strerror(errno));
    }

    if (pthread_attr_destroy(&attr)) {
        lprintf(fatal, "pthread_attr_destroy(): %d, %s\n", errno,
                strerror(errno));
    }
}

/**
 * \brief Reads a segment of size 'len' starting at 'offset_start'.
 * \param[in] cf The cache instance.
 * \param[out] output_buf Output buffer to read data into.
 * \param[in] len Length of the data to read.
 * \param[in] offset_start The offset to start reading from.
 * \return The number of bytes read, or negative on error.
 *
 * \details Concurrency Architecture & State Machine:
 * ----------------------------------------
 * This function supports multiple concurrent segment downloads to allow
 * parallel FUSE reading. To prevent race conditions, memory leaks, and
 * duplicate downloads:
 *
 * 1. Mutual Exclusion & Locking:
 *    - `w_lock` guards writing to the cache files (`dfp`/`mfp`) and checking
 * segment existence.
 *    - `dl_lock` guards access to `active_dls`, which tracks currently active
 * downloads.
 *    - Ordering: ALWAYS lock `w_lock` before `dl_lock` to avoid deadlocks.
 *
 * 2. Active Download Tracking (`active_dls`):
 *    - A linked list of `ActiveDownload` nodes tracks the offsets currently
 * being downloaded.
 *    - If a segment is not cached but is already being downloaded by another
 * thread, subsequent FUSE threads will detect the node and wait via
 * `PTHREAD_COND_WAIT` on `dl_cond`.
 *
 * 3. Early-Return Copy:
 *    - While waiting, threads can perform early returns by copying data
 * directly from the in-progress `TransferStruct`'s memory buffer (`ts->data`)
 * once enough bytes have been received.
 *
 * 4. Double-Checked Locking:
 *    - Because locks must be released when launching threads or checking
 * semaphores, other threads could concurrently insert download trackers. We use
 * double-checked locking inside the background thread launcher and `sync_dl`
 * fallback path to verify that the download is still not tracked before
 * allocating a new node.
 */
static long Cache_read_segment(Cache *cf, char *const output_buf,
                               const off_t len, const off_t offset_start)
{
    if (cf->link->content_length == 0 || offset_start < 0
        || offset_start >= (off_t)cf->link->content_length) {
        return 0;
    }

    long send;
    off_t dl_offset = offset_start / cf->blksz * cf->blksz;
    int ret;

retry:
    PTHREAD_MUTEX_LOCK(&cf->w_lock);
    if (Seg_exist(cf, dl_offset)) {
        send = Data_read(cf, (uint8_t *)output_buf, len, offset_start);
        PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
        goto bgdl;
    }

    PTHREAD_MUTEX_LOCK(&cf->dl_lock);
    ActiveDownload *ad = ActiveDownload_find(cf, dl_offset);
    if (ad != NULL) {
        PTHREAD_MUTEX_UNLOCK(&cf->w_lock);

        while ((ad = ActiveDownload_find(cf, dl_offset)) != NULL) {
            if (ad->ts && ad->ts->data
                && ad->ts->curr_size
                       >= (size_t)(offset_start - dl_offset + len)) {
                memcpy(output_buf, ad->ts->data + (offset_start - dl_offset),
                       len);
                PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
                send = len;
                goto bgdl;
            }
            PTHREAD_COND_WAIT(&cf->dl_cond, &cf->dl_lock);
        }
        PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
        PTHREAD_MUTEX_LOCK(&cf->w_lock);
        if (Seg_exist(cf, dl_offset)) {
            send = Data_read(cf, (uint8_t *)output_buf, len, offset_start);
            PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
            goto bgdl;
        }
        goto sync_dl;
    }
    PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);

    /*
     * Attempt to launch a background download thread if the background thread
     * slot is available (bgt_sem > 0). This acts as a throttle on concurrent
     * background prefetches.
     */
    ret = sem_trywait(&cf->bgt_sem);
    if (ret == 0) {
        PTHREAD_MUTEX_LOCK(&cf->dl_lock);
        /*
         * Double-checked locking: Re-verify that another thread hasn't already
         * added this offset to the active downloads list while we were
         * unlocked.
         */
        ActiveDownload *bg_ad = ActiveDownload_find(cf, dl_offset);
        if (bg_ad == NULL) {
            ActiveDownload_add(cf, dl_offset);
            PTHREAD_COND_BROADCAST(&cf->dl_cond);
            PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
            Cache_bgdl_launcher(cf, dl_offset);
            PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
            goto retry;
        }
        PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
        SEM_POST(&cf->bgt_sem); /* Back off and release the slot */
    }

sync_dl:
    PTHREAD_MUTEX_LOCK(&cf->dl_lock);
    /*
     * Double-checked locking: Verify that another thread hasn't concurrently
     * started a download for this offset. If it has, back off, release the
     * locks, and retry to join the wait loop.
     */
    ActiveDownload *sync_ad = ActiveDownload_find(cf, dl_offset);
    if (sync_ad != NULL) {
        PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
        PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
        goto retry;
    }
    ActiveDownload_add(cf, dl_offset);
    PTHREAD_COND_BROADCAST(&cf->dl_cond);
    PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);

    PTHREAD_MUTEX_UNLOCK(&cf->w_lock);

    uint8_t *recv_buf = CALLOC(cf->blksz, sizeof(uint8_t));
    long recv
        = Link_download(cf->link, (char *)recv_buf, cf->blksz, dl_offset, cf);

    PTHREAD_MUTEX_LOCK(&cf->w_lock);

    if (recv < 0) {
        PTHREAD_MUTEX_LOCK(&cf->dl_lock);
        ActiveDownload_remove(cf, dl_offset);
        PTHREAD_COND_BROADCAST(&cf->dl_cond);
        PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
        FREE(recv_buf);
        PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
        return recv;
    }
    if ((recv == cf->blksz)
        || ((uintmax_t)dl_offset
            == (cf->link->content_length / (size_t)cf->blksz
                * (size_t)cf->blksz))) {
        if (recv < (offset_start - dl_offset) + len) {
            lprintf(error,
                    "received %ld bytes, but required at least %ld bytes\n",
                    recv, (long)((offset_start - dl_offset) + len));
            PTHREAD_MUTEX_LOCK(&cf->dl_lock);
            ActiveDownload_remove(cf, dl_offset);
            PTHREAD_COND_BROADCAST(&cf->dl_cond);
            PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
            FREE(recv_buf);
            PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
            return -EIO;
        }
        if (Data_write(cf, recv_buf, recv, dl_offset) == recv) {
            Seg_set(cf, dl_offset, 1);
        }
    } else {
        lprintf(error,
                "received %ld rather than %d, possible network "
                "error.\n",
                recv, cf->blksz);
        PTHREAD_MUTEX_LOCK(&cf->dl_lock);
        ActiveDownload_remove(cf, dl_offset);
        PTHREAD_COND_BROADCAST(&cf->dl_cond);
        PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
        FREE(recv_buf);
        PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
        return -EIO;
    }

    PTHREAD_MUTEX_LOCK(&cf->dl_lock);
    ActiveDownload_remove(cf, dl_offset);
    PTHREAD_COND_BROADCAST(&cf->dl_cond);
    PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
    send = len;
    if (offset_start < dl_offset
        || (size_t)(offset_start - dl_offset) + (size_t)send
               > (size_t)cf->blksz) {
        lprintf(error, "invalid offset or length for memcpy, aborting copy\n");
        send = 0;
    } else {
        memcpy(output_buf, recv_buf + (offset_start - dl_offset), send);
    }
    FREE(recv_buf);
    PTHREAD_MUTEX_UNLOCK(&cf->w_lock);

bgdl: {
}
    off_t next_dl_offset = dl_offset + cf->blksz;
    int next_seg_missing = 0;
    if ((uintmax_t)next_dl_offset < (uintmax_t)cf->link->content_length) {
        PTHREAD_MUTEX_LOCK(&cf->w_lock);
        next_seg_missing = !Seg_exist(cf, next_dl_offset);
        PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
    }
    if (next_seg_missing) {
        ret = sem_trywait(&cf->bgt_sem);
        if (!ret) {
            PTHREAD_MUTEX_LOCK(&cf->w_lock);
            next_seg_missing = !Seg_exist(cf, next_dl_offset);
            PTHREAD_MUTEX_LOCK(&cf->dl_lock);
            const ActiveDownload *next_ad
                = ActiveDownload_find(cf, next_dl_offset);
            if (next_seg_missing && next_ad == NULL) {
                ActiveDownload_add(cf, next_dl_offset);
                PTHREAD_COND_BROADCAST(&cf->dl_cond);
                PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
                PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
                Cache_bgdl_launcher(cf, next_dl_offset);
            } else {
                PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
                PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
                SEM_POST(&cf->bgt_sem);
            }
        }
    }

    return send;
}

long Cache_read(Cache *cf, char *const output_buf, off_t len,
                const off_t offset_start)
{
    off_t send = 0;
    for (off_t start = offset_start, end; len > 0;
         len -= end - start, start = end) {
        end = start / cf->blksz * cf->blksz + cf->blksz;
        if (end > start + len) {
            end = start + len;
        }
        long seg_send = Cache_read_segment(
            cf, output_buf + (start - offset_start), end - start, start);
        if (seg_send < 0) {
            return seg_send;
        }
        send += seg_send;
        if (seg_send < end - start) {
            break;
        }
    }
    return send;
}
