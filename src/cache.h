#ifndef CACHE_H
#define CACHE_H

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
 *      - segment count (int)
 *      - individual segments (array of seg)
 *
 * \note
 *   - apologies for whoever is going to be reading this. Sorry for being so
 * verbose in this header file, this is probably one of the most challenging
 * thing I have ever written so far! Yes I am doing a PhD in computer science,
 * but it doesn't imply that I am good at computer science or programming!
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
 * \brief cache metadata structure
 * \note fanf2@cam.ac.uk told me to use an array rather than linked list!
 */
typedef struct {
    const char *filename; /**< the filename from the http server */
    const char *metapath; /**< the path to the metadata file*/
    const char *datapath; /**< the path to the cache file */
    long len; /**<the size of the file */
    int nseg; /**<the number of segments */
    Seg *seg; /**< the detail of each segment */
 } Cache;

/***************************** To be completed ******************************/

/**
 * \brief Read from a cache file set
 * \details This function performs the following two things:
 *  - check again the metafile to see which segments are available
 *  - return the available segments
 */
long Cache_read(const char *filepath, long offset, long len);

/**
 * \brief Write to a cache file set
 * \details This function performs the following two things:
 *  - Write to the data file
 *  - Update the metadata file
 */
long Cache_write(const char *filepath, long offset, long len,
                   const uint8_t *content);

/**
 * \brief create a cache file
 */
Cache *Cache_create(const char *filepath, long len);

/**
 * \brief open a cache file
 */
Cache *Cache_open(const char *filepath);

/**************************** Completed functions ****************************/


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
 */
int Meta_read(Cache *cf);

/**
 * \brief create a data file
 * \details We use sparse creation here
 * \return 1 on success, 0 on failure
 */
int Data_create(Cache *cf, long len);

/**
 * \brief read a data file
 * \return -1 when the data file does not exist
 */
long Data_read(const Cache *cf, long offset, long len,
                uint8_t *buf);

/**
 * \brief write to a data file
 */
long Data_write(const Cache *cf, long offset, long len,
                 const uint8_t *buf);

#endif
