#ifndef CACHE_H
#define CACHE_H

#include <unistd.h>

/**
 * \file cache.h
 * \brief cache related structures and functions
 * \details think of these as some sort of generic caching layer.
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
    long *start;
    long *end;
} Seg;

/**
 * \brief cache metadata structure
 * \note fanf2@cam.ac.uk told me to use an array rather than linked list!
 */
typedef struct {
    const char* filepath;
    long len;
    FILE *data_fd; /**< the file descriptor for the data file */
    FILE *meta_fd; /**< the file descriptor for the meta file */
    int nseg; /**<the number of segments */
    Seg *seg; /**< the detail of each segment */
 } Cache;

/**************************** External functions ******************************/

/**
 * \brief read from a cache file
 */
long Cache_read(const char *filepath, long offset, long len);

/**
 * \brief write to a cache file
 */
long Cache_write(const char *filepath, long offset, long len,
                   const uint8_t *content);

/**************************** Internal functions ******************************/

/**
 * \brief create a cache file
 */
int Cache_create(const char *filepath, long len);

/**
 * \brief open a cache file
 */
Cache *Cache_open(const char *filepath);

/**
 * \brief write a metadata file
 */
int Meta_write(Cache *cf);

/**
 * \brief read a metadata file
 */
int Meta_read(Cache *cf);

/**
 * \brief read a data file
 * \return -1 when the data file does not exist
 */
long Data_read(Cache *cf, long offset, long len,
                const uint8_t *buf);

/**
 * \brief write to a data file
 */
long Data_write(Cache *cf, long offset, long len,
                 const uint8_t *buf);
/**
 * \brief create a data file
 * \details We use sparse creation here
 * \return 1 on success, 0 on failure
 */
int Data_create(Cache *cf, long len);
#endif
