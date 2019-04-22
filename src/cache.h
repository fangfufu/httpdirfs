#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>
#include <unistd.h>

/**
 * \file cache.h
 * \brief cache related structures and functions
 * \details
 * Metadata:
 *   - We store the metadata and the actual data separately in two
 * different folders.
 *   - The metadata file should follow the following format:
 *      - file length (long)
 *      - CURLINFO_FILETIME
 *      - segment count (int)
 *      - individual segments (array of seg)
 * \note
 *   - We are using 'long' to store file size, because the offset in fseek() is
 * in long, because the way we use the cache system, you cannot seek past
 * long. So the biggest file size has to be able to be stored in long. This
 * makes this program architecturally dependent, i.e. i386 vs amd64
 */

/**
 * \brief Type definition for a cache segment
 */
typedef uint8_t Seg;

/**
 * \brief cache in-memory data structure
 */
typedef struct {
    char *filename; /**< the filename from the http server */
    long time; /**<the modified time of the file */
    long len; /**<the size of the file */
    int blksz; /**<the block size of the data file */
    long segbc; /**<segment array byte count */
    Seg *seg; /**< the detail of each segment */
} Cache;

/***************************** To be completed ******************************/

/**
 * \brief Read from a cache file set
 * \details This function performs the following two things:
 *  - check again the metafile to see which segments are available
 *  - return the available segments
 */
long Cache_read(const char *fn, long offset, long len, uint8_t *buf);

/**
 * \brief Write to a cache file set
 * \details This function performs the following two things:
 *  - Write to the data file
 *  - Update the metadata file
 */
long Cache_write(const char *fn, long offset, long len,
                   const uint8_t *buf);

/****************************** Work in progress *****************************/


/**************************** Completed functions ****************************/

/**
 * \brief initialise the cache system directories
 * \details This function basically sets up the following variables:
 *  - META_DIR
 *  - DATA_DIR
 *
 * If these directories do not exist, they will be created.
 */
void CacheSystem_init(const char *dir);

/**
 * \brief Check if a segment exists.
 */
int Seg_exist(Cache *cf, long start);

/**
 * \brief Set the existence of a segment
 * \param[in] start the starting position of the segment.
 * \param[in] i 1 for exist, 0 for doesn't exist
 */
void Seg_set(Cache *cf, long start, int i);


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
int Meta_create(Cache *cf);

/**
 * \brief write a metadata file
 * \return
 *  - -1 on error,
 *  - 0 on success
 */
int Meta_write(const Cache *cf);

/**
 * \brief read a metadata file
 * \return
 *  - -1 on fread error,
 *  - -2 on metadata internal inconsistency
 *  - 0 on success
 */
int Meta_read(Cache *cf);

/**
 * \brief create a data file
 * \details We use sparse creation here
 * \return
 *  - 0 on successful creation of the data file, note that the result of
 * the ftruncate() is ignored.
 *  - -1 on failure to create the data file.
 */
int Data_create(Cache *cf);

/**
 * \brief obtain the data file size
 */
long Data_size(const char *fn);

/**
 * \brief read a data file
 * \return
 *  - -1 when the data file does not exist
 *  - otherwise, the number of bytes read.
 */
long Data_read(const Cache *cf, long offset, long len,
                uint8_t *buf);

/**
 * \brief write to a data file
 * \return
 *  - -1 when the data file does not exist
 *  - otherwise, the number of bytes written.
 */
long Data_write(const Cache *cf, long offset, long len,
                 const uint8_t *buf);

/**
 * \brief Create directories under the cache directory structure, if they do
 * not already exist
 * \return
 *  -   -1 failed to create metadata directory.
 *  -   -2 failed to create data directory.
 *  -   -3 failed to create both metadata and data directory.
 * \note This should be called every time a new LinkTable is created.
 */
int CacheDir_create(const char *fn);

/**
 * \brief Allocate a new cache data structure
 */
Cache *Cache_alloc();

/**
 * \brief free a cache data structure
 */
void Cache_free(Cache *cf);

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
int Cache_exist(const char *fn);

/**
 * \brief create a cache file set
 */
Cache *Cache_create(const char *fn, long len, long time);

/**
 * \brief delete a cache file set
 */
void Cache_delete(const char *fn);

/**
 * \brief open a cache file set
 */
Cache *Cache_open(const char *fn);
#endif
