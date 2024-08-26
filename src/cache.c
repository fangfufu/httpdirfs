#include "cache.h"

#include "config.h"
#include "log.h"
#include "util.h"

#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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

/**
 * \brief Calculate cache system directory path
 */
static char *CacheSystem_calc_dir(const char *url)
{
    char *xdg_cache_home = getenv("XDG_CACHE_HOME");
    if (!xdg_cache_home) {
        char *home = getenv("HOME");
        char *xdg_cache_home_default = "/.cache";
        xdg_cache_home = path_append(home, xdg_cache_home_default);
    }
    if (mkdir
            (xdg_cache_home, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
            && (errno != EEXIST)) {
        lprintf(fatal, "mkdir(): %s\n", strerror(errno));
    }
    char *cache_dir_root = path_append(xdg_cache_home, "/httpdirfs/");
    if (mkdir
            (cache_dir_root, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
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
    FREE(cache_dir_root);
    curl_free(escaped_url);
    curl_easy_cleanup(c);
    return full_path;
}

void CacheSystem_init(const char *path, int url_supplied)
{
    lprintf(cache_lock_debug,
            "thread %x: initialise cf_lock;\n", pthread_self());
    if (pthread_mutex_init(&cf_lock, NULL)) {
        lprintf(fatal, "cf_lock initialisation failed!\n");
    }

    if (url_supplied) {
        path = CacheSystem_calc_dir(path);
    }

    lprintf(debug, "%s\n", path);

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
        if (mkdir
                (sonic_path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
                && (errno != EEXIST)) {
            lprintf(fatal, "mkdir(): %s\n", strerror(errno));
        }
        FREE(sonic_path);

        /*
         * Create "rest" sub-directory for DATA_DIR
         */
        sonic_path = path_append(DATA_DIR, "rest/");
        if (mkdir
                (sonic_path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
                && (errno != EEXIST)) {
            lprintf(fatal, "mkdir(): %s\n", strerror(errno));
        }
        FREE(sonic_path);
    }

    CACHE_SYSTEM_INIT = 1;
}

/**
 * \brief read a metadata file
 * \return 0 on success, errno on error.
 */
static int Meta_read(Cache *cf)
{
    FILE *fp = cf->mfp;
    rewind(fp);

    int nmemb = 0;

    if (!fp) {
        /*
         * The metadata file does not exist
         */
        lprintf(error, "fopen(): %s\n", strerror(errno));
        return EIO;
    }

    if (    1 != fread(&cf->time, sizeof(long), 1, fp) ||
            1 != fread(&cf->content_length, sizeof(off_t), 1, fp) ||
            1 != fread(&cf->blksz, sizeof(int), 1, fp) ||
            1 != fread(&cf->segbc, sizeof(long), 1, fp) ||
            ferror(fp) ) {
        lprintf(error, "error reading core metadata %s!\n", cf->path);
        return EIO;
    }

    /* These things really should not be zero!!! */
    if (!cf->content_length || !cf->blksz || !cf->segbc) {
        lprintf(error,
                "corruption: content_length: %ld, blksz: %d, segbc: %ld\n",
                cf->content_length, cf->blksz, cf->segbc);
        return EBADMSG;
    }

    if (cf->blksz != CONFIG.data_blksz) {
        lprintf(warning, "Warning: cf->blksz != CONFIG.data_blksz\n");
    }

    if (cf->segbc > CONFIG.max_segbc) {
        lprintf(error, "Error: segbc: %ld\n", cf->segbc);
        return EFBIG;
    }

    /*
     * Allocate memory for all segments, and read them in
     */
    cf->seg = CALLOC(cf->segbc, sizeof(Seg));
    nmemb = fread(cf->seg, sizeof(Seg), cf->segbc, fp);

    /*
     * We shouldn't have gone past the end of the file
     */
    if (feof(fp)) {
        /*
         * reached EOF
         */
        lprintf(error, "attempted to read past the end of the \
file!\n");
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
    rewind(fp);

    if (!fp) {
        /*
         * Cannot create the metadata file
         */
        lprintf(error, "fopen(): %s\n", strerror(errno));
        return -1;
    }

    /*
     * These things really should not be zero!!!
     */
    if (!cf->content_length || !cf->blksz || !cf->segbc) {
        lprintf(error, "content_length: %ld, blksz: %d, segbc: %ld\n",
                cf->content_length, cf->blksz, cf->segbc);
    }

    fwrite(&cf->time, sizeof(long), 1, fp);
    fwrite(&cf->content_length, sizeof(off_t), 1, fp);
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
    if (ftruncate(fd, cf->content_length)) {
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
static long Data_size(const char *fn)
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
    if (len == 0) {
        lprintf(error, "requested to read 0 byte!\n");
        return -EINVAL;
    }

    lprintf(cache_lock_debug,
            "thread %x: locking seek_lock;\n", pthread_self());
    PTHREAD_MUTEX_LOCK(&cf->seek_lock);

    long byte_read = 0;

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

    /*
     * Calculate how much to read
     */
    if (offset + len > cf->content_length) {
        len -= offset + len - cf->content_length;
        if (len < 0) {
            goto end;
        }
    }

    byte_read = fread(buf, sizeof(uint8_t), len, cf->dfp);
    if (byte_read != len) {
        lprintf(debug,
                "fread(): requested %ld, returned %ld!\n", len, byte_read);
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

    lprintf(cache_lock_debug,
            "thread %x: unlocking seek_lock;\n", pthread_self());
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
static long Data_write(Cache *cf, const uint8_t *buf, off_t len,
                       off_t offset)
{
    if (len == 0) {
        /*
         * We should permit empty files
         */
        return 0;
    }

    lprintf(cache_lock_debug,
            "thread %x: locking seek_lock;\n", pthread_self());
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
        lprintf(error,
                "fwrite(): requested %ld, returned %ld!\n",
                len, byte_written);
    }

    if (ferror(cf->dfp)) {
        /*
         * filesystem error
         */
        lprintf(error, "fwrite(): encountered error!\n");
    }

end:
    lprintf(cache_lock_debug,
            "thread %x: unlocking seek_lock;\n", pthread_self());
    PTHREAD_MUTEX_UNLOCK(&cf->seek_lock);
    return byte_written;
}

int CacheDir_create(const char *dirn)
{
    char *metadirn = path_append(META_DIR, dirn);
    char *datadirn = path_append(DATA_DIR, dirn);
    int i;

    i = -mkdir(metadirn, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    if (i && (errno != EEXIST)) {
        lprintf(fatal, "mkdir(): %s\n", strerror(errno));
    }

    i |= -mkdir(datadirn,
                S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) << 1;
    if (i && (errno != EEXIST)) {
        lprintf(fatal, "mkdir(): %s\n", strerror(errno));
    }
    FREE(datadirn);
    FREE(metadirn);
    return -i;
}

/**
 * \brief Allocate a new cache data structure
 */
static Cache *Cache_alloc()
{
    Cache *cf = CALLOC(1, sizeof(Cache));

    if (pthread_mutex_init(&cf->seek_lock, NULL)) {
        lprintf(fatal, "seek_lock initialisation failed!\n");
    }

    if (pthread_mutex_init(&cf->w_lock, NULL)) {
        lprintf(fatal, "w_lock initialisation failed!\n");
    }

    if (pthread_mutexattr_init(&cf->bgt_lock_attr)) {
        lprintf(fatal, "bgt_lock_attr initialisation failed!\n");
    }

    if (pthread_mutexattr_setpshared(&cf->bgt_lock_attr,
                                     PTHREAD_PROCESS_SHARED)) {
        lprintf(fatal, "could not set bgt_lock_attr!\n");
    }

    if (pthread_mutex_init(&cf->bgt_lock, &cf->bgt_lock_attr)) {
        lprintf(fatal, "bgt_lock initialisation failed!\n");
    }

    return cf;
}

/**
 * \brief free a cache data structure
 */
static void Cache_free(Cache *cf)
{
    if (pthread_mutex_destroy(&cf->seek_lock)) {
        lprintf(fatal, "could not destroy seek_lock!\n");
    }

    if (pthread_mutex_destroy(&cf->w_lock)) {
        lprintf(fatal, "could not destroy w_lock!\n");
    }

    if (pthread_mutex_destroy(&cf->bgt_lock)) {
        lprintf(fatal, "could not destroy bgt_lock!\n");
    }

    if (pthread_mutexattr_destroy(&cf->bgt_lock_attr)) {
        lprintf(fatal, "could not destroy bgt_lock_attr!\n");
    }

    if (cf->path) {
        FREE(cf->path);
    }

    if (cf->seg) {
        FREE(cf->seg);
    }

    if (cf->fs_path) {
        FREE(cf->fs_path);
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
    lprintf(debug, "metafn: %s\n", metafn);
    char *datafn = path_append(DATA_DIR, fn);
    lprintf(debug, "datafn: %s\n", datafn);
    /*
     * access() returns 0 on success
     */
    int no_meta = access(metafn, F_OK);
    int no_data = access(datafn, F_OK);

    if (no_meta ^ no_data) {
        lprintf(warning, "Cache file partially missing.\n");
        if (no_meta) {
            lprintf(debug, "Unlinking datafn: %s\n", datafn);
            if (unlink(datafn)) {
                lprintf(fatal, "unlink(): %s\n", strerror(errno));
            }
        }
        if (no_data) {
            lprintf(debug, "Unlinking metafn: %s\n", metafn);
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
    if (CONFIG.mode == SONIC) {
        Link *link = path_to_Link(fn);
        fn = link->sonic.id;
    }

    char *metafn = path_append(META_DIR, fn);
    char *datafn = path_append(DATA_DIR, fn);
    if (!access(metafn, F_OK)) {
        if (unlink(metafn)) {
            lprintf(error, "unlink(): %s\n", strerror(errno));
        }
    }

    if (!access(datafn, F_OK)) {
        if (unlink(datafn)) {
            lprintf(error, "unlink(): %s\n", strerror(errno));
        }
    }
    FREE(metafn);
    FREE(datafn);
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
        lprintf(error,
                "cannot close metadata after creation: %s.\n",
                strerror(errno));
    }
    FREE(metafn);
}

int Cache_create(const char *path)
{
    Link *this_link = path_to_Link(path);

    char *fn = "__UNINITIALISED__";

    if (CONFIG.mode == NORMAL) {
        fn = curl_easy_unescape(NULL,
                                this_link->f_url + ROOT_LINK_OFFSET, 0,
                                NULL);
    } else if (CONFIG.mode == SINGLE) {
        fn = curl_easy_unescape(NULL, this_link->linkname, 0, NULL);
    } else if (CONFIG.mode == SONIC) {
        fn = this_link->sonic.id;
    } else {
        lprintf(fatal, "Invalid CONFIG.mode\n");
    }
    lprintf(debug, "Creating cache files for %s.\n", fn);

    Cache *cf = Cache_alloc();
    cf->path = strndup(fn, MAX_PATH_LEN);
    cf->time = this_link->time;
    cf->content_length = this_link->content_length;
    cf->blksz = CONFIG.data_blksz;
    cf->segbc = (cf->content_length / cf->blksz) + 1;
    cf->seg = CALLOC(cf->segbc, sizeof(Seg));

    Meta_create(cf);

    if (Meta_open(cf)) {
        Cache_free(cf);
        lprintf(error, "cannot open metadata file, %s.\n", fn);
    }

    if (Meta_write(cf)) {
        lprintf(error, "Meta_write() failed!\n");
    }

    if (fclose(cf->mfp)) {
        lprintf(error,
                "cannot close metadata after write, %s.\n",
                strerror(errno));
    }

    Data_create(cf);

    Cache_free(cf);

    int res = Cache_exist(fn);

    if (res) {
        lprintf(fatal, "Cache file creation failed for %s\n", path);
    }

    if (CONFIG.mode == NORMAL) {
        curl_free(fn);
    } else if (CONFIG.mode == SONIC) {
        curl_free(fn);
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

    lprintf(cache_lock_debug,
            "thread %x: locking cf_lock;\n", pthread_self());
    PTHREAD_MUTEX_LOCK(&cf_lock);

    if (link->cache_ptr) {
        link->cache_ptr->cache_opened++;

        lprintf(cache_lock_debug,
                "thread %x: unlocking cf_lock;\n", pthread_self());
        PTHREAD_MUTEX_UNLOCK(&cf_lock);
        return link->cache_ptr;
    }

    /*
     * Check if both metadata and data file exist
     */
    if (CONFIG.mode == NORMAL || CONFIG.mode == SINGLE) {
        if (Cache_exist(fn)) {

            lprintf(cache_lock_debug,
                    "thread %x: unlocking cf_lock;\n", pthread_self());
            PTHREAD_MUTEX_UNLOCK(&cf_lock);
            return NULL;
        }
    } else if (CONFIG.mode == SONIC) {
        if (Cache_exist(link->sonic.id)) {

            lprintf(cache_lock_debug,
                    "thread %x: unlocking cf_lock;\n", pthread_self());
            PTHREAD_MUTEX_UNLOCK(&cf_lock);
            return NULL;
        }
    } else {
        lprintf(fatal, "Invalid CONFIG.mode\n");
    }

    /*
     * Create the cache in-memory data structure
     */
    Cache *cf = Cache_alloc();

    /*
     * Fill in the fs_path
     */
    cf->fs_path = CALLOC(MAX_PATH_LEN + 1, sizeof(char));
    strncpy(cf->fs_path, fn, MAX_PATH_LEN);

    /*
     * Set the path for the local cache file, if we are in sonic mode
     */
    if (CONFIG.mode == SONIC) {
        fn = link->sonic.id;
    }

    cf->path = strndup(fn, MAX_PATH_LEN);

    /*
     * Associate the cache structure with a link
     */
    cf->link = link;

    if (Meta_open(cf)) {
        Cache_free(cf);
        lprintf(error, "cannot open metadata file %s.\n", fn);

        lprintf(cache_lock_debug,
                "thread %x: unlocking cf_lock;\n", pthread_self());
        PTHREAD_MUTEX_UNLOCK(&cf_lock);
        return NULL;
    }

    /*
     * Corrupt metadata
     */
    if (Meta_read(cf)) {
        Cache_free(cf);
        lprintf(error, "metadata error: %s.\n", fn);

        lprintf(cache_lock_debug,
                "thread %x: unlocking cf_lock;\n", pthread_self());
        PTHREAD_MUTEX_UNLOCK(&cf_lock);
        return NULL;
    }

    /*
     * Inconsistency between metadata and data file, note that on disk file
     * size might be bigger than content_length, due to on-disk filesystem
     * allocation policy.
     */
    if (cf->content_length > Data_size(fn)) {
        lprintf(error, "metadata inconsistency %s, \
cf->content_length: %ld, Data_size(fn): %ld.\n", fn, cf->content_length,
                Data_size(fn));
        Cache_free(cf);

        lprintf(cache_lock_debug,
                "thread %x: unlocking cf_lock;\n", pthread_self());
        PTHREAD_MUTEX_UNLOCK(&cf_lock);
        return NULL;
    }

    /*
     * Check if the cache files are not outdated
     */
    if (cf->time != cf->link->time) {
        lprintf(warning, "outdated cache file: %s.\n", fn);
        Cache_free(cf);

        lprintf(cache_lock_debug,
                "thread %x: unlocking cf_lock;\n", pthread_self());
        PTHREAD_MUTEX_UNLOCK(&cf_lock);
        return NULL;
    }

    if (Data_open(cf)) {
        Cache_free(cf);
        lprintf(error, "cannot open data file %s.\n", fn);

        lprintf(cache_lock_debug,
                "thread %x: unlocking cf_lock;\n", pthread_self());
        PTHREAD_MUTEX_UNLOCK(&cf_lock);
        return NULL;
    }

    cf->cache_opened = 1;
    /*
     * Yup, we just created a circular loop. ;)
     */
    cf->link->cache_ptr = cf;

    lprintf(cache_lock_debug,
            "thread %x: unlocking cf_lock;\n", pthread_self());
    PTHREAD_MUTEX_UNLOCK(&cf_lock);
    return cf;
}

void Cache_close(Cache *cf)
{
    lprintf(cache_lock_debug,
            "thread %x: locking cf_lock;\n", pthread_self());
    PTHREAD_MUTEX_LOCK(&cf_lock);

    cf->cache_opened--;

    if (cf->cache_opened > 0) {

        lprintf(cache_lock_debug,
                "thread %x: unlocking cf_lock;\n", pthread_self());
        PTHREAD_MUTEX_UNLOCK(&cf_lock);
        return;
    }

    if (Meta_write(cf)) {
        lprintf(error, "Meta_write() error.");
    }

    if (fclose(cf->mfp)) {
        lprintf(error, "cannot close metadata: %s.\n", strerror(errno));
    }

    if (fclose(cf->dfp)) {
        lprintf(error, "cannot close data file %s.\n", strerror(errno));
    }

    cf->link->cache_ptr = NULL;

    lprintf(cache_lock_debug,
            "thread %x: unlocking cf_lock;\n", pthread_self());
    PTHREAD_MUTEX_UNLOCK(&cf_lock);
    Cache_free(cf);
}

/**
 * \brief Check if a segment exists.
 * \return 1 if the segment exists
 */
static int Seg_exist(Cache *cf, off_t offset)
{
    off_t byte = offset / cf->blksz;
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
    off_t byte = offset / cf->blksz;
    cf->seg[byte] = i;
}

/**
 * \brief Background download function
 * \details If we are requesting the data from the second half of the current
 * segment, we can spawn a pthread using this function to download the next
 * segment.
 */
static void *Cache_bgdl(void *arg)
{
    Cache *cf = (Cache *) arg;

    lprintf(cache_lock_debug, "thread %x: locking w_lock;\n",
            pthread_self());
    PTHREAD_MUTEX_LOCK(&cf->w_lock);

    uint8_t *recv_buf = CALLOC(cf->blksz, sizeof(uint8_t));
    lprintf(debug, "thread %x spawned.\n ", pthread_self());
    long recv = Link_download(cf->link, (char *) recv_buf, cf->blksz,
                              cf->next_dl_offset);
    if (recv < 0) {
        lprintf(error, "thread %x received %ld bytes, \
which doesn't make sense\n", pthread_self(), recv);
    }

    if ((recv == cf->blksz) ||
            (cf->next_dl_offset ==
             (cf->content_length / cf->blksz * cf->blksz))) {
        Data_write(cf, recv_buf, recv, cf->next_dl_offset);
        Seg_set(cf, cf->next_dl_offset, 1);
    } else {
        lprintf(error, "received %ld rather than %ld, possible network \
error.\n", recv, cf->blksz);
    }

    FREE(recv_buf);

    lprintf(cache_lock_debug,
            "thread %x: unlocking bgt_lock;\n", pthread_self());
    PTHREAD_MUTEX_UNLOCK(&cf->bgt_lock);

    lprintf(cache_lock_debug,
            "thread %x: unlocking w_lock;\n", pthread_self());
    PTHREAD_MUTEX_UNLOCK(&cf->w_lock);

    if (pthread_detach(pthread_self())) {
        lprintf(error, "%s\n", strerror(errno));
    };
    pthread_exit(NULL);
}

long
Cache_read(Cache *cf, char *const output_buf, const off_t len,
           const off_t offset_start)
{
    long send;

    /*
     * The offset of the segment to be downloaded
     */
    off_t dl_offset = (offset_start + len) / cf->blksz * cf->blksz;

    /*
     * ------------- Check if the segment already exists --------------
     */
    if (Seg_exist(cf, dl_offset)) {
        send = Data_read(cf, (uint8_t *) output_buf, len, offset_start);
        goto bgdl;
    } else {
        /*
         * Wait for any other download thread to finish
         */

        lprintf(cache_lock_debug,
                "thread %ld: locking w_lock;\n", pthread_self());
        PTHREAD_MUTEX_LOCK(&cf->w_lock);

        if (Seg_exist(cf, dl_offset)) {
            /*
             * The segment now exists - it was downloaded by another
             * download thread. Send it off and unlock the I/O
             */
            send =
                Data_read(cf, (uint8_t *) output_buf, len, offset_start);

            lprintf(cache_lock_debug,
                    "thread %x: unlocking w_lock;\n", pthread_self());
            PTHREAD_MUTEX_UNLOCK(&cf->w_lock);

            goto bgdl;
        }
    }

    /*
     * ------------------ Download the segment ---------------------
     */

    uint8_t *recv_buf = CALLOC(cf->blksz, sizeof(uint8_t));
    lprintf(debug, "thread %x: spawned.\n ", pthread_self());
    long recv = Link_download(cf->link, (char *) recv_buf, cf->blksz,
                              dl_offset);
    if (recv < 0) {
        lprintf(error, "thread %x received %ld bytes, \
which doesn't make sense\n", pthread_self(), recv);
    }
    /*
     * check if we have received enough data, write it to the disk
     *
     * Condition 1: received the exact amount as the segment size.
     * Condition 2: offset is the last segment
     */
    if ((recv == cf->blksz) ||
            (dl_offset == (cf->content_length / cf->blksz * cf->blksz))) {
        Data_write(cf, recv_buf, recv, dl_offset);
        Seg_set(cf, dl_offset, 1);
    } else {
        lprintf(error, "received %ld rather than %ld, possible network \
error.\n", recv, cf->blksz);
    }
    FREE(recv_buf);
    send = Data_read(cf, (uint8_t *) output_buf, len, offset_start);

    lprintf(cache_lock_debug,
            "thread %x: unlocking w_lock;\n", pthread_self());
    PTHREAD_MUTEX_UNLOCK(&cf->w_lock);

    /*
     * ----------- Download the next segment in background -----------------
     */
bgdl: {
    }
    off_t next_dl_offset = round_div(offset_start, cf->blksz) * cf->blksz;
    if ((next_dl_offset > dl_offset) && !Seg_exist(cf, next_dl_offset)
            && next_dl_offset < cf->content_length) {
        /*
         * Stop the spawning of multiple background pthreads
         */
        if (!pthread_mutex_trylock(&cf->bgt_lock)) {
            lprintf(cache_lock_debug,
                    "thread %x: trylocked bgt_lock;\n", pthread_self());
            cf->next_dl_offset = next_dl_offset;
            if (pthread_create(&cf->bgt, NULL, Cache_bgdl, cf)) {
                lprintf(error,
                        "Error creating background download thread\n");
            }
        }
    }

    return send;
}
