#include "cache.h"

#include "link.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <sys/stat.h>
#include <sys/types.h>

/**
 * \brief Data file block size
 * \details We set it to 1024*1024 = 1048576 bytes
 */
#define DATA_BLK_SZ         1048576

/**
 * \brief the maximum length of a path
 * \details This corresponds the maximum path length under Ext4.
 */
#define MAX_PATH_LEN        4096

/**
 * \brief error associated with metadata
 */
typedef enum {
    SUCCESS = 0,    /**< Metadata read successful */
    EFREAD  = -1,   /**< Fread failed */
    EINCON  = -2,   /**< Inconsistency in metadata */
    EZEROS  = -3    /**< Unexpected zeros in metadata */
} MetaError;

int CACHE_SYSTEM_INIT = 0;

/**
 * \brief the receive buffer
 */
static uint8_t RECV_BUF[DATA_BLK_SZ];

/**
 * \brief The metadata directory
 */
static char *META_DIR;

/**
 * \brief The data directory
 */
static char *DATA_DIR;

void CacheSystem_init(const char *path)
{
    DIR* dir;

    /*
     * Check if the top-level cache directory exists, if not, exit the
     * program. We don't want to unintentionally create a folder
     */
    dir = opendir(path);
    if (dir) {
        closedir(dir);
    } else {
        fprintf(stderr,
                "CacheSystem_init(): opendir(): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Handle the case of missing '/' */
    if (path[strnlen(path, MAX_PATH_LEN) - 1] == '/') {
        META_DIR = strndupcat(path, "meta/", MAX_PATH_LEN);
        DATA_DIR = strndupcat(path, "data/", MAX_PATH_LEN);
    } else {
        META_DIR = strndupcat(path, "/meta/", MAX_PATH_LEN);
        DATA_DIR = strndupcat(path, "/data/", MAX_PATH_LEN);
    }

    /* Check if directories exist, if not, create them */
    if (mkdir(META_DIR, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)
        && (errno != EEXIST)) {
        fprintf(stderr, "CacheSystem_init(): mkdir(): %s\n",
                    strerror(errno));
    }
    if (mkdir(DATA_DIR, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)
        && (errno != EEXIST)) {
        fprintf(stderr, "CacheSystem_init(): mkdir(): %s\n",
                strerror(errno));
    }

    CACHE_SYSTEM_INIT = 1;
}

/**
 * \brief read a metadata file
 * \return
 *  - -1 on fread error,
 *  - -2 on metadata internal inconsistency
 *  - 0 on success
 */
static int Meta_read(Cache *cf)
{
    FILE *fp;
    char *metafn = strndupcat(META_DIR, cf->path, MAX_PATH_LEN);
    fp = fopen(metafn, "r");
    free(metafn);
    int res = 0;
    int nmemb = 0;

    if (!fp) {
        /* The metadata file does not exist */
        fprintf(stderr, "Meta_read(): fopen(): %s\n", strerror(errno));
        return EFREAD;
    }

    fread(&(cf->time), sizeof(long), 1, fp);
    fread(&(cf->content_length), sizeof(off_t), 1, fp);
    fread(&(cf->blksz), sizeof(int), 1, fp);
    fread(&(cf->segbc), sizeof(long), 1, fp);

    /* Error checking for fread */
    if (ferror(fp)) {
        fprintf(stderr,
                "Meta_read(): error reading core metadata!\n");
    }

    /* These things really should not be zero!!! */
    if (!cf->content_length || !cf->blksz || !cf->segbc) {
        fprintf(stderr,
                "Meta_read:() Warning corrupt metadata: content_length: %ld, \
blksz: %d, segbc: %ld\n", cf->content_length, cf->blksz, cf->segbc);
        res = EZEROS;
        goto end;
    }

    /* Allocate some memory for the segment */
    cf->seg = calloc(cf->segbc, sizeof(Seg));
    if (!cf->seg) {
        fprintf(stderr, "Meta_read(): segbc: %ld, calloc failure: %s\n",
                cf->segbc, strerror(errno));
        exit(EXIT_FAILURE);
    }
    /* Read all the segment */
    nmemb = fread(cf->seg, sizeof(Seg), cf->segbc, fp);

    /* Error checking for fread */
    if (ferror(fp)) {
        fprintf(stderr,
                "Meta_read(): error reading bitmap!\n");
        res = EFREAD;
    }

    /* Check for inconsistent metadata file */
    if (nmemb != cf-> segbc) {
        fprintf(stderr,
                "Meta_read(): corrupted metadata!\n");
        res = EINCON;
    }

    end:
    if (fclose(fp)) {
        fprintf(stderr, "Meta_read(): fclose(): %s\n", strerror(errno));
    }
    return res;
}

/**
 * \brief write a metadata file
 * \return
 *  - -1 on error,
 *  - 0 on success
 */
static int Meta_write(const Cache *cf)
{
    FILE *fp;
    char *metafn = strndupcat(META_DIR, cf->path, MAX_PATH_LEN);
    fp = fopen(metafn, "w");
    free(metafn);
    int res = 0;

    if (!fp) {
        /* Cannot create the metadata file */
        fprintf(stderr, "Meta_write(): fopen(): %s\n", strerror(errno));
        return -1;
    }

    /* WARNING: These things really should not be zero!!! */
    if (!cf->content_length || !cf->blksz || !cf->segbc) {
        fprintf(stderr,
                "Meta_write:() Warning: content_length: %ld, blksz: %d, segbc: \
%ld\n", cf->content_length, cf->blksz, cf->segbc);
    }

    fwrite(&(cf->time), sizeof(long), 1, fp);
    fwrite(&(cf->content_length), sizeof(off_t), 1, fp);
    fwrite(&(cf->blksz), sizeof(int), 1, fp);
    fwrite(&(cf->segbc), sizeof(long), 1, fp);
    fwrite(cf->seg, sizeof(Seg), cf->segbc, fp);

    /* Error checking for fwrite */
    if (ferror(fp)) {
        fprintf(stderr,
                "Meta_write(): fwrite(): encountered error (from ferror)!\n");
        res = -1;
    }

    if (fclose(fp)) {
        fprintf(stderr, "Meta_write(): fclose(): %s\n", strerror(errno));
    }
    return res;
}

/**
 * \brief create a metadata file
 * \details We set the followings here:
 *  -   block size
 *  -   the number of segments
 *
 * The number of segments depends on the block size. The block size is set to
 * DATA_BLK_SZ for now. In future support for different block size may be
 * implemented.
 */
static int Meta_create(Cache *cf)
{
    cf->blksz = DATA_BLK_SZ;
    cf->segbc = cf->content_length / cf->blksz + 1;
    cf->seg = calloc(cf->segbc, sizeof(Seg));
    if (!cf->seg) {
        fprintf(stderr, "Meta_create(): calloc failure!\n");
        exit(EXIT_FAILURE);
    }
    return Meta_write(cf);
}

/**
 * \brief create a data file
 * \details We use sparse creation here
 * \return
 *  - 0 on successful creation of the data file, note that the result of
 * the ftruncate() is ignored.
 *  - -1 on failure to create the data file.
 */
static int Data_create(Cache *cf)
{
    int fd;
    int mode;

    mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    char *datafn = strndupcat(DATA_DIR, cf->path, MAX_PATH_LEN);
    fd = open(datafn, O_WRONLY | O_CREAT, mode);
    free(datafn);
    if (fd == -1) {
        fprintf(stderr, "Data_create(): open(): %s\n", strerror(errno));
        return -1;
    }
    if (ftruncate(fd, cf->content_length)) {
        fprintf(stderr, "Data_create(): ftruncate(): %s\n", strerror(errno));
    }
    if (close(fd)) {
        fprintf(stderr, "Data_create(): close:(): %s\n", strerror(errno));
    }
    return 0;
}

/**
 * \brief obtain the data file size
 */
static long Data_size(const char *fn)
{
    char *datafn = strndupcat(DATA_DIR, fn, MAX_PATH_LEN);
    struct stat st;
    int s = stat(datafn, &st);
    free(datafn);
    if (!s) {
        return st.st_size;
    }
    fprintf(stderr, "Data_size(): stat(): %s\n", strerror(errno));
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
static long Data_read(const Cache *cf, uint8_t *buf, off_t len, off_t offset)
{
    if (len == 0) {
        fprintf(stderr, "Data_read(): requested to read 0 byte!\n");
        return -EINVAL;
    }

    size_t start = offset;
    size_t end = start + len;
    char range_str[64];
    snprintf(range_str, sizeof(range_str), "%lu-%lu", start, end);
    fprintf(stderr, "Data_read(%s, %s);\n", cf->path, range_str);

    long byte_read = -EIO;

    if (fseeko(cf->dfp, offset, SEEK_SET)) {
        /* fseeko failed */
        fprintf(stderr, "Data_read(): fseeko(): %s\n", strerror(errno));
        goto end;
    }

    byte_read = fread(buf, sizeof(uint8_t), len, cf->dfp);
    if (byte_read != len) {
        fprintf(stderr,
                "Data_read(): fread(): requested %ld, returned %ld!\n",
                len, byte_read);
        if (feof(cf->dfp)) {
            /* reached EOF */
            fprintf(stderr,
                    "Data_read(): fread(): reached the end of the file!\n");
        }
        if (ferror(cf->dfp)) {
            /* filesystem error */
            fprintf(stderr,
                "Data_read(): fread(): encountered error (from ferror)!\n");
        }
    }
    end:
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

static long Data_write(const Cache *cf, const uint8_t *buf, off_t len,
                       off_t offset)
{
    if (len == 0) {
        fprintf(stderr, "Data_write(): requested to write 0 byte!\n");
        return -EINVAL;
    }

    size_t start = offset;
    size_t end = start + len;
    char range_str[64];
    snprintf(range_str, sizeof(range_str), "%lu-%lu", start, end);
    fprintf(stderr, "Data_write(%s, %s);\n", cf->path, range_str);

    long byte_written = -EIO;

    if (fseeko(cf->dfp, offset, SEEK_SET)) {
        /* fseeko failed */
        fprintf(stderr, "Data_write(): fseeko(): %s\n", strerror(errno));
        goto end;
    }

    byte_written = fwrite(buf, sizeof(uint8_t), len, cf->dfp);
    if (byte_written != len) {
        fprintf(stderr,
                "Data_write(): fwrite(): requested %ld, returned %ld!\n",
                len, byte_written);
        if (feof(cf->dfp)) {
            /* reached EOF */
            fprintf(stderr,
                    "Data_write(): fwrite(): reached the end of the file!\n");
        }
        if (ferror(cf->dfp)) {
            /* filesystem error */
            fprintf(stderr,
                "Data_write(): fwrite(): encountered error (from ferror)!\n");
        }
    }
    end:
    return byte_written;
}

int CacheDir_create(const char *dirn)
{
    char *metadirn = strndupcat(META_DIR, dirn, MAX_PATH_LEN);
    char *datadirn = strndupcat(DATA_DIR, dirn, MAX_PATH_LEN);
    int i;

    i = -mkdir(metadirn, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (i && (errno != EEXIST)) {
        fprintf(stderr, "CacheDir_create(): mkdir(): %s\n", strerror(errno));
    }

    i |= -mkdir(datadirn, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) << 1;
    if (i && (errno != EEXIST)) {
        fprintf(stderr, "CacheDir_create(): mkdir(): %s\n", strerror(errno));
    }
    return -i;
}

/**
 * \brief Allocate a new cache data structure
 */
static Cache *Cache_alloc()
{
    Cache *cf = calloc(1, sizeof(Cache));
    if (!cf) {
        fprintf(stderr, "Cache_new(): calloc failure!\n");
        exit(EXIT_FAILURE);
    }
    return cf;
}

/**
 * \brief free a cache data structure
 */
static void Cache_free(Cache *cf)
{
    if (cf->path) {
        free(cf->path);
    }
    if (cf->seg) {
        free(cf->seg);
    }
    free(cf);
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
    int meta_exists = 1;
    int data_exists = 1;
    char *metafn = strndupcat(META_DIR, fn, MAX_PATH_LEN);
    char *datafn = strndupcat(DATA_DIR, fn, MAX_PATH_LEN);

    if (access(metafn, F_OK)) {
//         fprintf(stderr, "Cache_exist(): access(): %s\n", strerror(errno));
        meta_exists = 0;
    }

    if (access(datafn, F_OK)) {
//         fprintf(stderr, "Cache_exist(): access(): %s\n", strerror(errno));
        data_exists = 0;
    }

    if (meta_exists ^ data_exists) {
        if (meta_exists) {
            if(unlink(metafn)) {
                fprintf(stderr, "Cache_exist(): unlink(): %s\n",
                        strerror(errno));
            }
        }
        if (data_exists) {
            if(unlink(datafn)) {
                fprintf(stderr, "Cache_exist(): unlink(): %s\n",
                        strerror(errno));
            }
        }
    }

    free(metafn);
    free(datafn);

    return meta_exists & data_exists;
}

/**
 * \brief delete a cache file set
 */
void Cache_delete(const char *fn)
{
    char *metafn = strndupcat(META_DIR, fn, MAX_PATH_LEN);
    char *datafn = strndupcat(DATA_DIR, fn, MAX_PATH_LEN);
    if (!access(metafn, F_OK)) {
        if(unlink(metafn)) {
            fprintf(stderr, "Cache_delete(): unlink(): %s\n",
                    strerror(errno));
        }
    }

    if (!access(datafn, F_OK)) {
        if(unlink(datafn)) {
            fprintf(stderr, "Cache_delete(): unlink(): %s\n",
                    strerror(errno));
        }
    }
    free(metafn);
    free(datafn);
}

/**
 * \brief Open the data file of a cache data set
 * \return
 *  -   0 on success
 *  -   -1 on failure, with appropriate errno set.
 */
static int Data_open(Cache *cf)
{
    char *datafn = strndupcat(DATA_DIR, cf->path, MAX_PATH_LEN);
    cf->dfp = fopen(datafn, "r+");
    free(datafn);
    if (!cf->dfp) {
        /* Failed to open the data file */
        fprintf(stderr, "Data_open(): fopen(): %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int Cache_create(Link *this_link)
{
    char *fn;
    fn = curl_easy_unescape(
        NULL, this_link->f_url + ROOT_LINK_OFFSET, 0, NULL);
    if (Cache_exist(fn)) {
        /* We make sure that the cache files are not outdated */
        Cache *cf = Cache_open(fn);
        if (cf->time == this_link->time) {
            Cache_close(cf);
            curl_free(fn);
            return 0;
        }
        Cache_delete(fn);
    }

    fprintf(stderr, "Cache_create(): Creating cache files for %s.\n", fn);

    Cache *cf = Cache_alloc();
    cf->path = strndup(fn, MAX_PATH_LEN);
    cf->time = this_link->time;
    cf->content_length = this_link->content_length;

    if (Data_create(cf)) {
        fprintf(stderr, "Cache_create(): Data_create() failed!\n");
    }

    if (Meta_create(cf)) {
        fprintf(stderr, "Cache_create(): Meta_create() failed!\n");
    }

    Cache_free(cf);

    /*
     * Cache_exist() returns 1, if cache files exist and valid. Whereas this
     * function returns 0 on success.
     */
    int res = -(!Cache_exist(fn));
    curl_free(fn);
    return res;
}

Cache *Cache_open(const char *fn)
{
    fprintf(stderr, "Cache_open(): Opening cache file %s...", fn);

    /* Check if both metadata and data file exist */
    if (!Cache_exist(fn)) {
        fprintf(stderr, "Failure!\n");
        return NULL;
    }

    /* Create the cache in-memory data structure */
    Cache *cf = Cache_alloc();
    cf->path = strndup(fn, MAX_PATH_LEN);

    /* Associate the cache structure with a link */
    cf->link = path_to_Link(fn);
    if (!cf->link) {
        Cache_free(cf);
        return NULL;
    }

    /*
     * Internally inconsistent or corrupt metadata
     */
    int rtn = Meta_read(cf);
    if ((rtn == EINCON) || rtn == EZEROS) {
        Cache_free(cf);
        fprintf(stderr, "Failure!\nMetadata inconsistent or corrupt!\n");
        return NULL;
    }

    /*
     * Inconsistency between metadata and data file, note that on disk file
     * size might be bigger than content_length, due to on-disk filesystem
     * allocation policy.
     */
    if (cf->content_length > Data_size(fn)) {
        fprintf(stderr,
                "Failure!\nMetadata inconsistency: \
cf->content_length: %ld, Data_size(fn): %ld\n",
                cf->content_length, Data_size(fn));
        Cache_free(cf);
        return NULL;
    }

    if (Data_open(cf)) {
        Cache_free(cf);
        fprintf(stderr, "Failure!\n");
        return NULL;
    }

    fprintf(stderr, "Success!\n");
    return cf;
}

void Cache_close(Cache *cf)
{
    fprintf(stderr, "Cache_close(): Closing cache file %s.\n", cf->path);

    if (Meta_write(cf)) {
        fprintf(stderr, "Cache_close(): Meta_write() error.");
    }

    if (fclose(cf->dfp)) {
        fprintf(stderr, "Data_write(): fclose(): %s\n", strerror(errno));
    }
    return Cache_free(cf);
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
 * \param[in] offset the starting position of the segment.
 * \param[in] i 1 for exist, 0 for doesn't exist
 * \note Call this after downloading a segment.
 */
static void Seg_set(Cache *cf, off_t offset, int i)
{
    off_t byte = offset / cf->blksz;
    cf->seg[byte] = i;
}

long Cache_read(Cache *cf, char *output_buf, off_t len, off_t offset)
{
    long sent;
    /*
     * Quick fix for SIGFPE,
     * this shouldn't happen in the first place!
     */
    if (!cf->blksz) {
        fprintf(stderr,
                "Cache_read(): Warning: cf->blksz: %d, directly downloading",
                cf->blksz);
        return path_download(cf->path, output_buf, len, offset);
    }
    pthread_mutex_lock(&(cf->rw_lock));
    if (Seg_exist(cf, offset)) {
        /*
         * The metadata shows the segment already exists. This part is easy,
         * as you don't have to worry about alignment
         */
        sent = Data_read(cf, (uint8_t *) output_buf, len, offset);
    } else {
        /* Calculate the aligned offset */
        off_t dl_offset = offset / cf->blksz * cf->blksz;
        /* Download the segment */
        long recv = path_download(cf->path, (char *) RECV_BUF, cf->blksz,
                                  dl_offset);
        /* Send it off */
        memmove(output_buf, RECV_BUF + (offset-dl_offset), len);
        sent = len;
        /* Write it to the disk, check if we haven't received enough data*/
        if (recv == cf->blksz) {
            Data_write(cf, RECV_BUF, cf->blksz, dl_offset);
            Seg_set(cf, dl_offset, 1);
        } else if (dl_offset == (cf->content_length / cf->blksz * cf->blksz)) {
            /* Check if we are at the last block */
            Data_write(cf, RECV_BUF, cf->blksz, dl_offset);
            Seg_set(cf, dl_offset, 1);
        } else {
            fprintf(stderr,
            "Cache_read(): recv (%ld) < cf->blksz! Possible network error?\n",
                recv);
        }
    }
    pthread_mutex_unlock(&(cf->rw_lock));
    return sent;
}
