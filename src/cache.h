#ifndef CACHE_H
#define CACHE_H

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

/**
 * \file cache.h
 * \brief cache related structures and functions
 * \details
 *   - We store the metadata and the actual data separately in two
 * separate folders.
 */

/**
 * \brief Type definition for a cache segment
 */
typedef uint8_t Seg;

/**
 * \brief cache in-memory data structure
 */
typedef struct {
    char *path; /**< the path to the file on the web server */
    long time; /**<the modified time of the file */
    off_t content_length; /**<the size of the file */
    pthread_mutex_t rw_lock; /**< mutex for disk operation */
    FILE *dfp; /**< The FILE pointer for the cache data file*/
    int blksz; /**<the block size of the data file */
    long segbc; /**<segment array byte count */
    Seg *seg; /**< the detail of each segment */
} Cache;

/**
 * \brief whether the cache system is enabled
 */
extern int CACHE_SYSTEM_INIT;

/**
 * \brief initialise the cache system directories
 * \details This function basically sets up the following variables:
 *  - META_DIR
 *  - DATA_DIR
 *
 * If these directories do not exist, they will be created.
 * \note Called by parse_arg_list(), verified to be working
 */
void CacheSystem_init(const char *dir);

/**
 * \brief Create directories under the cache directory structure, if they do
 * not already exist
 * \return
 *  -   -1 failed to create metadata directory.
 *  -   -2 failed to create data directory.
 *  -   -3 failed to create both metadata and data directory.
 * \note Called by LinkTable_new(), verified to be working
 */
int CacheDir_create(const char *fn);

/**
 * \brief open a cache file set
 * \note This function is called by fs_open(), verified to be working.
 */
Cache *Cache_open(const char *fn);

/**
 * \brief Close a cache data structure
 * \note This function is called by fs_release(), verified to be working.
 */
void Cache_close(Cache *cf);

/**
 * \brief create a cache file set if it doesn't exist already
 * \return
 *  -   0, if the cache file already exists, or was created succesfully.
 *  -   -1, otherwise
 * \note Called by Link_set_stat(), verified to be working
 */
int Cache_create(const char *fn, long len, long time);

/***************************** To be completed ******************************/

/**
 * \brief Intelligently read from the cache system
 * \details If the segment does not exist on the local hard disk, download from
 * the Internet
 * \param[in] cf the cache in-memory data structure
 * \param[out] buf the output buffer
 * \param[in] size the requested segment size
 * \param[in] offset the start of the segment
 * \return the length of the segment the cache system managed to obtain.
 * \note Called by fs_read()
 */
long Cache_read(Cache *cf, char *buf, size_t size, off_t offset);

#endif
