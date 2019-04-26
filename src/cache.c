#include "cache.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * \brief Data file block size
 * \details We set it to 1024*1024*8 = 8MiB
 */

#define DEFAULT_DATA_BLK_SZ         8*1024*1024

/**
 * \brief Maximum segment block count
 * \details This is set to 1024*1024*1024 = 1 GB, which allows the user to
 * access a 8TB file.
 */
#define MAX_SEGBC           1073741824

/**
 * \brief error associated with metadata
 */
typedef enum {
    SUCCESS     =  0,   /**< Metadata read successful */
    EFREAD      = -1,   /**< Fread failed */
    EINCONSIST  = -2,   /**< Inconsistency in metadata */
    EZERO       = -3,   /**< Unexpected zeros in metadata */
    EMEM        = -4    /**< Memory allocation failure */
} MetaError;


int CACHE_SYSTEM_INIT = 0;
int DATA_BLK_SZ = 0;

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
        META_DIR = path_append(path, "meta/");
        DATA_DIR = path_append(path, "data/");
    } else {
        META_DIR = path_append(path, "/meta/");
        DATA_DIR = path_append(path, "/data/");
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

    if (!DATA_BLK_SZ) {
        DATA_BLK_SZ = DEFAULT_DATA_BLK_SZ;
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
    FILE *fp = cf->mfp;
    rewind(fp);

    int nmemb = 0;

    if (!fp) {
        /* The metadata file does not exist */
        fprintf(stderr, "Meta_read(): fopen(): %s\n", strerror(errno));
        return EFREAD;
    }

    fread(&cf->time, sizeof(long), 1, fp);
    fread(&cf->content_length, sizeof(off_t), 1, fp);
    fread(&cf->blksz, sizeof(int), 1, fp);
    fread(&cf->segbc, sizeof(long), 1, fp);

    /* Error checking for fread */
    if (ferror(fp)) {
        fprintf(stderr,
                "Meta_read(): error reading core metadata!\n");
    }

    /* These things really should not be zero!!! */
    if (!cf->content_length || !cf->blksz || !cf->segbc) {
        fprintf(stderr,
                "Meta_read(): corrupt metadata: %s, content_length: %ld, \
blksz: %d, segbc: %ld\n", cf->path, cf->content_length, cf->blksz, cf->segbc);
        return EZERO;
    }

    /* Allocate some memory for the segment */
    if (cf->segbc > MAX_SEGBC) {
        fprintf(stderr, "Meta_read(): Error: segbc: %ld\n", cf->segbc);
        return EMEM;
    }
    cf->seg = calloc(cf->segbc, sizeof(Seg));
    if (!cf->seg) {
        fprintf(stderr, "Meta_read(): calloc failure: %s\n", strerror(errno));
        return EMEM;
    }
    /* Read all the segment */
    nmemb = fread(cf->seg, sizeof(Seg), cf->segbc, fp);

    /* We shouldn't have gone past the end of the file */
    if (feof(fp)) {
        /* reached EOF */
        fprintf(stderr,
                "Meta_read(): attempted to read past the end of the file!\n");
        return EINCONSIST;
    }

    /* Error checking for fread */
    if (ferror(fp)) {
        fprintf(stderr,
                "Meta_read(): error reading bitmap!\n");
        return EFREAD;
    }

    /* Check for inconsistent metadata file */
    if (nmemb != cf-> segbc) {
        fprintf(stderr,
                "Meta_read(): corrupted metadata!\n");
        return EINCONSIST;
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
        /* Cannot create the metadata file */
        fprintf(stderr, "Meta_write(): fopen(): %s\n", strerror(errno));
        return -1;
    }

    /* These things really should not be zero!!! */
    if (!cf->content_length || !cf->blksz || !cf->segbc) {
        fprintf(stderr,
                "Meta_write(): Warning: content_length: %ld, blksz: %d, segbc: \
%ld\n", cf->content_length, cf->blksz, cf->segbc);
    }

    fwrite(&cf->time, sizeof(long), 1, fp);
    fwrite(&cf->content_length, sizeof(off_t), 1, fp);
    fwrite(&cf->blksz, sizeof(int), 1, fp);
    fwrite(&cf->segbc, sizeof(long), 1, fp);
    fwrite(cf->seg, sizeof(Seg), cf->segbc, fp);

    /* Error checking for fwrite */
    if (ferror(fp)) {
        fprintf(stderr,
                "Meta_write(): fwrite(): encountered error (from ferror)!\n");
        return -1;
    }

    return 0;
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
    char *datafn = path_append(DATA_DIR, cf->path);
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
    char *datafn = path_append(DATA_DIR, fn);
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
static long Data_read(Cache *cf, uint8_t *buf, off_t len, off_t offset)
{
    if (len == 0) {
        fprintf(stderr, "Data_read(): requested to read 0 byte!\n");
        return -EINVAL;
    }

    if (fseeko(cf->dfp, offset, SEEK_SET)) {
        /* fseeko failed */
        fprintf(stderr, "Data_read(): fseeko(): %s\n", strerror(errno));
        return -EIO;
    }

    long byte_read = fread(buf, sizeof(uint8_t), len, cf->dfp);
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
        fprintf(stderr, "Data_write(): requested to write 0 byte!\n");
        return -EINVAL;
    }


    if (fseeko(cf->dfp, offset, SEEK_SET)) {
        /* fseeko failed */
        fprintf(stderr, "Data_write(): fseeko(): %s\n", strerror(errno));
        return -EIO;
    }

    long byte_written = fwrite(buf, sizeof(uint8_t), len, cf->dfp);
    if (byte_written != len) {
        fprintf(stderr,
                "Data_write(): fwrite(): requested %ld, returned %ld!\n",
                len, byte_written);
        if (ferror(cf->dfp)) {
            /* filesystem error */
            fprintf(stderr,
                "Data_write(): fwrite(): encountered error (from ferror)!\n");
        }
    }

    return byte_written;
}

int CacheDir_create(const char *dirn)
{
    char *metadirn = path_append(META_DIR, dirn);
    char *datadirn = path_append(DATA_DIR, dirn);
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

    if (pthread_mutexattr_init(&cf->rw_lock_attr)) {
        fprintf(stderr,
                "Cache_alloc(): rw_lock_attr initialisation failed!\n");
    }

    if (pthread_mutexattr_setpshared(&cf->rw_lock_attr,
        PTHREAD_PROCESS_SHARED)) {
        fprintf(stderr, "Cache_alloc(): could not set rw_lock_attr!\n");
    }

    if (pthread_mutex_init(&cf->rw_lock, &cf->rw_lock_attr)) {
        fprintf(stderr, "Cache_alloc(): rw_lock initialisation failed!\n");
    }

    if (pthread_mutexattr_init(&cf->bgt_lock_attr)) {
        fprintf(stderr,
                "Cache_alloc(): bgt_lock_attr initialisation failed!\n");
    }

    if (pthread_mutexattr_setpshared(&cf->bgt_lock_attr,
        PTHREAD_PROCESS_SHARED)) {
        fprintf(stderr, "Cache_alloc(): could not set bgt_lock_attr!\n");
    }

    if (pthread_mutex_init(&cf->bgt_lock, &cf->bgt_lock_attr)) {
        fprintf(stderr, "Cache_alloc(): bgt_lock initialisation failed!\n");
    }

    return cf;
}

/**
 * \brief free a cache data structure
 */
static void Cache_free(Cache *cf)
{
    if (pthread_mutex_destroy(&cf->rw_lock)) {
        fprintf(stderr, "Cache_free(): could not destroy rw_lock!\n");
    }

    if (pthread_mutexattr_destroy(&cf->rw_lock_attr)) {
        fprintf(stderr, "Cache_alloc(): could not destroy rw_lock_attr!\n");
    }

    if (pthread_mutex_destroy(&cf->bgt_lock)) {
        fprintf(stderr, "Cache_free(): could not destroy bgt_lock!\n");
    }

    if (pthread_mutexattr_destroy(&cf->bgt_lock_attr)) {
        fprintf(stderr, "Cache_alloc(): could not destroy bgt_lock_attr!\n");
    }

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
    char *metafn = path_append(META_DIR, fn);
    char *datafn = path_append(DATA_DIR, fn);

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
    char *metafn = path_append(META_DIR, fn);
    char *datafn = path_append(DATA_DIR, fn);
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
    char *datafn = path_append(DATA_DIR, cf->path);
    cf->dfp = fopen(datafn, "r+");
    free(datafn);
    if (!cf->dfp) {
        /* Failed to open the data file */
        fprintf(stderr, "Data_open(): fopen(%s): %s\n", datafn,
                strerror(errno));
        return -1;
    }
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
        /* Failed to open the data file */
        fprintf(stderr, "Meta_open(): fopen(%s): %s\n", metafn,
                strerror(errno));
        free(metafn);
        return -1;
    }
    free(metafn);
    return 0;
}

/**
 * \brief Create a metafile
 * \return
 *  -   0 on success
 *  -   -1 on failure, with appropriate errno set.
 */
static int Meta_create(Cache *cf)
{
    char *metafn = path_append(META_DIR, cf->path);
    cf->mfp = fopen(metafn, "w");
    if (!cf->mfp) {
        /* Failed to open the data file */
        fprintf(stderr, "Meta_create(): fopen(%s): %s\n", metafn,
                strerror(errno));
        free(metafn);
        return -1;
    }
    free(metafn);
    return 0;
}

int Cache_create(Link *this_link)
{
    char *fn;
    fn = curl_easy_unescape(NULL, this_link->f_url + ROOT_LINK_OFFSET, 0, NULL);
    fprintf(stderr, "Cache_create(): Creating cache files for %s.\n", fn);

    Cache *cf = Cache_alloc();
    cf->path = strndup(fn, MAX_PATH_LEN);
    cf->time = this_link->time;
    cf->content_length = this_link->content_length;
    cf->blksz = DATA_BLK_SZ;
    cf->segbc = (cf->content_length / cf->blksz) + 1;
    cf->seg = calloc(cf->segbc, sizeof(Seg));

    if (!cf->seg) {
        fprintf(stderr, "Cache_create(): cf->seg calloc failure!\n");
        exit(EXIT_FAILURE);
    }

    if (Meta_create(cf)) {
        fprintf(stderr, "Cache_create(): cannot create metadata.\n");
    }

    if (fclose(cf->mfp)) {
        fprintf(stderr,
                "Cache_create(): cannot close metadata after creation: %s.\n",
                strerror(errno));
    }

    if (Meta_open(cf)) {
        Cache_free(cf);
        fprintf(stderr, "Cache_create(): cannot open metadata file, %s.\n", fn);
    }

    if (Meta_write(cf)) {
        fprintf(stderr, "Cache_create(): Meta_write() failed!\n");
    }

    if (fclose(cf->mfp)) {
        fprintf(stderr,
                "Cache_create(): cannot close metadata after write, %s.\n",
                strerror(errno));
    }

    if (Data_create(cf)) {
        fprintf(stderr, "Cache_create(): Data_create() failed!\n");
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
    /* Check if both metadata and data file exist */
    if (!Cache_exist(fn)) {
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

    if (Meta_open(cf)) {
        Cache_free(cf);
        fprintf(stderr, "Cache_open(): cannot open metadata file %s.\n", fn);
        return NULL;
    }

    int rtn = Meta_read(cf);

    /*
     * Internally inconsistent or corrupt metadata
     */
    if ((rtn == EINCONSIST) || (rtn == EZERO) || (rtn == EMEM)) {
        Cache_free(cf);
        fprintf(stderr, "Cache_open(): metadata error: %s, %d.\n", fn, rtn);
        return NULL;
    }

    /*
     * Inconsistency between metadata and data file, note that on disk file
     * size might be bigger than content_length, due to on-disk filesystem
     * allocation policy.
     */
    if (cf->content_length > Data_size(fn)) {
        fprintf(stderr, "Cache_open(): metadata inconsistency %s, \
cf->content_length: %ld, Data_size(fn): %ld.\n", fn, cf->content_length,
                Data_size(fn));
        Cache_free(cf);
        return NULL;
    }

    /* Check if the cache files are not outdated */
    if (cf->time != cf->link->time) {
        fprintf(stderr, "Cache_open(): outdated cache file: %s.\n", fn);
        Cache_free(cf);
        return NULL;
    }

    if (Data_open(cf)) {
        Cache_free(cf);
        fprintf(stderr, "Cache_open(): cannot open data file %s.\n", fn);
        return NULL;
    }

    return cf;
}

void Cache_close(Cache *cf)
{
    /* Must wait for the background download thread to stop */
    pthread_mutex_lock(&cf->bgt_lock);
    pthread_mutex_unlock(&cf->bgt_lock);

    if (Meta_write(cf)) {
        fprintf(stderr, "Cache_close(): Meta_write() error.");
    }

    if (fclose(cf->mfp)) {
        fprintf(stderr, "Cache_close(): cannot close metadata: %s.\n",
                strerror(errno));
    }

    if (fclose(cf->dfp)) {
        fprintf(stderr, "Cache_close(): cannot close data file %s.\n",
                strerror(errno));
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
    pthread_mutex_lock(&cf->rw_lock);
    uint8_t *recv_buf = calloc(DATA_BLK_SZ, sizeof(uint8_t));
    fprintf(stderr, "Cache_bgdl(): ");
    long recv = path_download(cf->path, (char *) recv_buf, cf->blksz,
                              cf->next_offset);
    if ( (recv == cf->blksz) ||
        (cf->next_offset == (cf->content_length / cf->blksz * cf->blksz)) )
    {
        Data_write(cf, recv_buf, cf->blksz, cf->next_offset);
        Seg_set(cf, cf->next_offset, 1);
    }  else {
        fprintf(stderr, "Cache_bgdl(): recv(%ld)<cf->blksz, content_length: \
%ld, next_offset: %ld, possible network error?\n", recv, cf->content_length,
cf->next_offset);
    }
    free(recv_buf);
    pthread_mutex_unlock(&cf->bgt_lock);
    pthread_mutex_unlock(&cf->rw_lock);
    pthread_exit(NULL);
}

long Cache_read(Cache *cf, char *output_buf, off_t len, off_t offset)
{
//     size_t start = offset;
//     size_t end = start + len;
//     char range_str[64];
//     snprintf(range_str, sizeof(range_str), "%lu-%lu", start, end);
//     fprintf(stderr, "Cache_read(%s, %s);\n", cf->path, range_str);

    /* SIGFPE prevention, although this shouldn't happen in the first place! */
    if (!cf->blksz) {
        fprintf(stderr,
                "Cache_read(): Warning: cf->blksz: %d, directly downloading",
                cf->blksz);
        return path_download(cf->path, output_buf, len, offset);
    }

    long send;
    off_t dl_offset = offset / cf->blksz * cf->blksz;

    /* ------------------ Check if the segment already exists ---------------*/
    if (Seg_exist(cf, offset)) {
        send = Data_read(cf, (uint8_t *) output_buf, len, offset);
        goto bgdl;
    } else {
        /* Wait for the other I/O threads to finish, then lock */
        pthread_mutex_lock(&cf->rw_lock);
        /* Wait for the background download thread to finish */
        pthread_mutex_lock(&cf->bgt_lock);
        pthread_mutex_unlock(&cf->bgt_lock);
        if (Seg_exist(cf, offset)) {
            /* The segment already exists, send it off the unlock the I/O */
            send = Data_read(cf, (uint8_t *) output_buf, len, offset);
            pthread_mutex_unlock(&cf->rw_lock);
            goto bgdl;
        }
    }

    /* ------------------------Download the segment -------------------------*/

    uint8_t *recv_buf = calloc(DATA_BLK_SZ, sizeof(uint8_t));
    fprintf(stderr, "Cache_read(): ");
    long recv = path_download(cf->path, (char *) recv_buf, cf->blksz,
                                dl_offset);
    /*
        * check if we have received enough data
        * send it off, then write it to the disk
        *
        * Condition 1: received the exact amount as the segment size.
        * Condition 2: offset is the last segment
        */
    if ( (recv == cf->blksz) ||
        (dl_offset == (cf->content_length / cf->blksz * cf->blksz)) )
    {
        memmove(output_buf, recv_buf + (offset - dl_offset), len);
        send = len;
        Data_write(cf, recv_buf, cf->blksz, dl_offset);
        Seg_set(cf, dl_offset, 1);
    }  else {
        memmove(output_buf, recv_buf + (offset - dl_offset), recv);
        send = recv;
        fprintf(stderr,
        "Cache_read(): recv (%ld) < cf->blksz! Possible network error?\n",
            recv);
    }
    free(recv_buf);
    pthread_mutex_unlock(&cf->rw_lock);

    /* -----------Download the next segment in background -------------------*/
    bgdl:
    cf->next_offset = round_div(offset, cf->blksz) * cf->blksz;
    if ( (cf->next_offset > dl_offset) &&
        !Seg_exist(cf, cf->next_offset) &&
        cf->next_offset < cf->content_length ){
        /* Stop the spawning of multiple background pthreads */
        if(!pthread_mutex_trylock(&cf->bgt_lock)) {
            if (pthread_create(&cf->bgt, NULL, Cache_bgdl, cf)) {
                fprintf(stderr,
                    "Cache_read(): Error creating background download thread\n"
                );
            }
        }
    }

    return send;
}
