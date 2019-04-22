#include "cache.h"

#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/**
 * \brief Data file block size
 * \details The data file block size is set to 128KiB, for convenience. This is
 * because the maximum requested block size by FUSE seems to be 128KiB under
 * Debian Stretch. Note that the minimum requested block size appears to be
 * 4KiB.
 *
 * More information regarding block size can be found at:
 * https://wiki.vuze.com/w/Torrent_Piece_Size
 *
 * Note that at the current configuration, a 16GiB file uses 16MiB of memory to
 * store the bitmap
 */
#define DATA_BLK_SZ         131072

/**
 * \brief the maximum length of a path
 * \details This corresponds the maximum path length under Ext4.
 */
#define MAX_PATH_LEN        4096

/**
 * \brief create a metadata file
 * \details We set the followings here:
 *  -   block size
 *  -   the number of segments
 *
 * The number of segments depends on the block size. The block size is set to
 * 128KiB for now. In future support for different block size may be
 * implemented.
 */
static int Meta_create(Cache *cf);

/**
 * \brief write a metadata file
 * \return
 *  - -1 on error,
 *  - 0 on success
 */
static int Meta_write(const Cache *cf);

/**
 * \brief read a metadata file
 * \return
 *  - -1 on fread error,
 *  - -2 on metadata internal inconsistency
 *  - 0 on success
 */
static int Meta_read(Cache *cf);

/**
 * \brief create a data file
 * \details We use sparse creation here
 * \return
 *  - 0 on successful creation of the data file, note that the result of
 * the ftruncate() is ignored.
 *  - -1 on failure to create the data file.
 */
static int Data_create(Cache *cf);

/**
 * \brief obtain the data file size
 */
static long Data_size(const char *fn);

/**
 * \brief read a data file
 * \return
 *  - -1 when the data file does not exist
 *  - otherwise, the number of bytes read.
 */
static long Data_read(const Cache *cf, long offset, long len,
                      uint8_t *buf);

/**
 * \brief write to a data file
 * \return
 *  - -1 when the data file does not exist
 *  - otherwise, the number of bytes written.
 */
static long Data_write(const Cache *cf, long offset, long len,
                       const uint8_t *buf);

/**
 * \brief Allocate a new cache data structure
 */
static Cache *Cache_alloc();

/**
 * \brief free a cache data structure
 */
static void Cache_free(Cache *cf);

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
static int Cache_exist(const char *fn);

/**
 * \brief delete a cache file set
 */
static void Cache_delete(const char *fn);

int CACHE_SYSTEM_INIT = 0;

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

int Seg_exist(Cache *cf, long start)
{
    long total_bit = start / cf->blksz;
    long byte = total_bit / 8;
    int bit = total_bit % 8;

    return cf->seg[byte] & (1 << bit);
}

void Seg_set(Cache *cf, long start, int i)
{
    long total_bit = start / cf->blksz;
    long byte = total_bit / 8;
    int bit = total_bit % 8;

    if (i) {
        cf->seg[byte] |= (1 << bit);
    } else {
        cf->seg[byte] &= ~(1 << bit);
    }
}

static int Meta_create(Cache *cf)
{
    cf->blksz = DATA_BLK_SZ;
    cf->segbc = cf->content_length / cf->blksz / 8 + 1;
    cf->seg = calloc(cf->segbc, sizeof(Seg));
    if (!cf->seg) {
        fprintf(stderr, "Meta_create(): calloc failure!\n");
        exit(EXIT_FAILURE);
    }
    return Meta_write(cf);
}

static int Meta_read(Cache *cf)
{
    FILE *fp;
    char *metafn = strndupcat(META_DIR, cf->p_url, MAX_PATH_LEN);
    fp = fopen(metafn, "r");
    free(metafn);
    int res = 0;
    int nmemb = 0;

    if (!fp) {
        /* The metadata file does not exist */
        fprintf(stderr, "Meta_read(): fopen(): %s\n", strerror(errno));
        return -1;
    }

    fread(&(cf->time), sizeof(long), 1, fp);
    fread(&(cf->content_length), sizeof(off_t), 1, fp);
    fread(&(cf->blksz), sizeof(int), 1, fp);
    fread(&(cf->segbc), sizeof(long), 1, fp);

    /* Allocate some memory for the segment */
    cf->seg = malloc(cf->segbc * sizeof(Seg));
    if (!cf->seg) {
        fprintf(stderr, "Meta_read(): malloc failure!\n");
        exit(EXIT_FAILURE);
    }
    /* Read all the segment */
    nmemb = fread(cf->seg, sizeof(Seg), cf->segbc, fp);

    /* Error checking for fread */
    if (ferror(fp)) {
        fprintf(stderr,
                "Meta_read(): fread(): encountered error (from ferror)!\n");
        res = -1;
    }

    /* Check for inconsistent metadata file */
    if (nmemb != cf-> segbc) {
        fprintf(stderr,
                "Meta_read(): corrupted metadata!\n");
        res = -2;
    }

    if (fclose(fp)) {
        fprintf(stderr, "Meta_read(): fclose(): %s\n", strerror(errno));
    }
    return res;
}

static int Meta_write(const Cache *cf)
{
    FILE *fp;
    char *metafn = strndupcat(META_DIR, cf->p_url, MAX_PATH_LEN);
    fp = fopen(metafn, "w");
    free(metafn);
    int res = 0;

    if (!fp) {
        /* Cannot create the metadata file */
        fprintf(stderr, "Meta_write(): fopen(): %s\n", strerror(errno));
        return -1;
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

static int Data_create(Cache *cf)
{
    int fd;
    int mode;

    mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    char *datafn = strndupcat(DATA_DIR, cf->p_url, MAX_PATH_LEN);
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

static long Data_size(const char *fn)
{
    char *datafn = strndupcat(DATA_DIR, fn, MAX_PATH_LEN);
    struct stat st;
    int s = stat(datafn, &st);
    free(datafn);
    if (!s) {
        return st.st_blksize;
    }
    fprintf(stderr, "Data_size(): stat(): %s\n", strerror(errno));
    return -1;
}

static long Data_read(const Cache *cf, long offset, long len,
               uint8_t *buf)
{
    if (len == 0) {
        fprintf(stderr, "Data_read(): requested to read 0 byte!\n");
        return -1;
    }

    FILE *fp;
    char *datafn = strndupcat(DATA_DIR, cf->p_url, MAX_PATH_LEN);
    fp = fopen(datafn, "r");
    free(datafn);
    long byte_read = -1;

    if (!fp) {
        /* Failed to open the data file */
        fprintf(stderr, "Data_read(): fopen(): %s\n", strerror(errno));
        return -1;
    }

    if (fseeko(fp, offset, SEEK_SET)) {
        /* fseeko failed */
        fprintf(stderr, "Data_read(): fseeko(): %s\n", strerror(errno));
        goto cleanup;
    }

    byte_read = fread(buf, sizeof(uint8_t), len, fp);
    if (byte_read != len) {
        fprintf(stderr,
                "Data_read(): fread(): requested %ld, returned %ld!\n",
                len, byte_read);
        if (feof(fp)) {
            /* reached EOF */
            fprintf(stderr,
                    "Data_read(): fread(): reached the end of the file!\n");
        }
        if (ferror(fp)) {
            /* filesystem error */
            fprintf(stderr,
                    "Data_read(): fread(): encountered error (from ferror)!\n");
        }
    }

    cleanup:
    if (fclose(fp)) {
        fprintf(stderr, "Data_read(): fclose(): %s\n", strerror(errno));
    }
    return byte_read;
}

static long Data_write(const Cache *cf, long offset, long len,
                const uint8_t *buf)
{
    if (len == 0) {
        fprintf(stderr, "Data_write(): requested to write 0 byte!\n");
        return -1;
    }

    FILE *fp;
    char *datafn = strndupcat(DATA_DIR, cf->p_url, MAX_PATH_LEN);
    fp = fopen(datafn, "r+");
    free(datafn);
    long byte_written = -1;

    if (!fp) {
        /* Failed to open the data file */
        fprintf(stderr, "Data_write(): fopen(): %s\n", strerror(errno));
        return -1;
    }

    if (fseeko(fp, offset, SEEK_SET)) {
        /* fseeko failed */
        fprintf(stderr, "Data_write(): fseeko(): %s\n", strerror(errno));
        goto cleanup;
    }

    byte_written = fwrite(buf, sizeof(uint8_t), len, fp);
    if (byte_written != len) {
        fprintf(stderr,
                "Data_write(): fwrite(): requested %ld, returned %ld!\n",
                len, byte_written);
        if (feof(fp)) {
            /* reached EOF */
            fprintf(stderr,
                    "Data_write(): fwrite(): reached the end of the file!\n");
        }
        if (ferror(fp)) {
            /* filesystem error */
            fprintf(stderr,
                "Data_write(): fwrite(): encountered error (from ferror)!\n");
        }
    }

    cleanup:
    if (fclose(fp)) {
        fprintf(stderr, "Data_write(): fclose(): %s\n", strerror(errno));
    }
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

static Cache *Cache_alloc()
{
    Cache *cf = calloc(1, sizeof(Cache));
    if (!cf) {
        fprintf(stderr, "Cache_new(): calloc failure!\n");
        exit(EXIT_FAILURE);
    }
    return cf;
}

void Cache_close(Cache *cf)
{
    fprintf(stderr, "Cache_close(): Creating cache files for %p.\n", cf);

    return Cache_free(cf);
}

static void Cache_free(Cache *cf)
{
    if (cf->p_url) {
        free(cf->p_url);
    }
    if (cf->seg) {
        free(cf->seg);
    }
    free(cf);
}

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

    return !(meta_exists & data_exists);
}

int Cache_create(const char *fn, long len, long time)
{
    fprintf(stderr, "Cache_create(): Creating cache files for %s.\n", fn);

    Cache *cf = Cache_alloc();

    cf->p_url = strndup(fn, MAX_PATH_LEN);
    cf->time = time;
    cf->content_length = len;

    if (Data_create(cf)) {
        fprintf(stderr, "Cache_create(): Data_create() failed!\n");
    }

    if (Meta_create(cf)) {
        fprintf(stderr, "Cache_create(): Meta_create() failed!\n");
    }

    Cache_free(cf);

    return Cache_exist(fn);
}

static void Cache_delete(const char *fn)
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

Cache *Cache_open(const char *fn)
{
    /* Check if both metadata and data file exist */
    if (Cache_exist(fn)) {
        return NULL;
    }

    /* Create the cache in-memory data structure */
    Cache *cf = Cache_alloc();
    cf->p_url = strndup(fn, MAX_PATH_LEN);

    /* Internal inconsistency metadata file */
    if (Meta_read(cf) == -2) {
        Cache_free(cf);
        Cache_delete(fn);
        return NULL;
    }

    /* Inconsistency between metadata and data file */
    if (cf->content_length != Data_size(fn)) {
        fprintf(stderr,
                "Cache_open(): metadata is inconsistent with the data file!\n");
        Cache_free(cf);
        Cache_delete(fn);
        return NULL;
    }

    return cf;
}
