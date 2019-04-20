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
 *
 * \note
 *   - We are using 'long' to store file size, because the offset in fseek() is
 * in long, because the way we use the cache system, you cannot seek past
 * long. So the biggest file size has to be able to be stored in long. This
 * makes this program architecturally dependent, but this is due to the
 * dependency to fseek().
 */

/**
 * \brief a cache segment
 */
typedef struct {
    long start;
    long end;
} Seg;

/**
 * \brief cache in-memory data structure
 * \note fanf2@cam.ac.uk told me to use an array rather than linked list!
 */
typedef struct {
    char *filename; /**< the filename from the http server */
    long len; /**<the size of the file */
    long time; /**<the modified time of the file */
    int nseg; /**<the number of segments */
    Seg *seg; /**< the detail of each segment */
} Cache;

/***************************** To be completed ******************************/

/**
 * \brief create a cache file set
 */
Cache *Cache_create(const char *fn, long len);

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

/**
 * \brief Create a new cache data structure
 */
Cache *Cache_new();

/**
 * \brief open a cache file set
 */
Cache *Cache_open(const char *fn);

/**************************** Completed functions ****************************/

/**
 * \brief initialise the cache system
 * \details This function basically sets up the following variables:
 *  - META_DIR
 *  - DATA_DIR
 */
void Cache_init(const char *dir);

/**
 * \brief free a cache data structure
 */
void Cache_free(Cache *cf);

/**
 * \brief write a metadata file
 */
int Meta_write(const Cache *cf);

/**
 * \brief read a metadata file
 * \return 0 on error, 1 on success
 */
int Meta_read(Cache *cf);

/**
 * \brief create a data file
 * \details We use sparse creation here
 * \return
 *  - 1 on successful creation of the data file, note that the result of
 * the ftruncate() is ignored.
 *  - 0 on failure to create the data file.
 */
int Data_create(Cache *cf);

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

#endif
