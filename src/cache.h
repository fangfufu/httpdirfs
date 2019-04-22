#ifndef CACHE_H
#define CACHE_H

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
    char *p_url; /**< the filename from the http server */
    long time; /**<the modified time of the file */
    off_t content_length; /**<the size of the file */
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
 * \note Called by parse_arg_list()
 */
void CacheSystem_init(const char *dir);

/**
 * \brief open a cache file set
 * \note This function is called by fs_open().
 */
Cache *Cache_open(const char *fn);

/**
 * \brief Close a cache data structure
 * \note This function is called by fs_release().
 */
void Cache_close(Cache *cf);

/***************************** Work in Progress ******************************/

/**
 * \brief Check if a segment exists.
 * \note Call this when deciding whether to download a file
 */
int Seg_exist(Cache *cf, long start);

/**
 * \brief Set the existence of a segment
 * \param[in] start the starting position of the segment.
 * \param[in] i 1 for exist, 0 for doesn't exist
 * \note Call this after downloading a segment.
 */
void Seg_set(Cache *cf, long start, int i);

/**
 * \brief create a cache file set
 *  * \return
 *  -   0, if the cache file was created succesfully
 *  -   -1, otherwise
 * \note Call this when creating a new LinkTable
 */
int Cache_create(const char *fn, long len, long time);

/**
 * \brief Create directories under the cache directory structure, if they do
 * not already exist
 * \return
 *  -   -1 failed to create metadata directory.
 *  -   -2 failed to create data directory.
 *  -   -3 failed to create both metadata and data directory.
 * \note Call this when creating a new LinkTable
 */
int CacheDir_create(const char *fn);

/***************************** To be completed ******************************/



/**
 * \brief Read from a cache file set
 * \details This function performs the following two things:
 *  - check again the metafile to see which segments are available
 *  - return the available segments
 * \note Call this when it is not necessary to download a segment
 */
long Cache_read(const char *fn, long offset, long len, uint8_t *buf);

/**
 * \brief Write to a cache file set
 * \details This function performs the following two things:
 *  - Write to the data file
 *  - Update the metadata file
 * \note Call this after downloading a segment.
 */
long Cache_write(const char *fn, long offset, long len, const uint8_t *buf);

#endif
