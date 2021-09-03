#ifndef CACHE_H
#define CACHE_H

/**
 * \file cache.h
 * \brief cache related structures and functions
 * \details
 *   - We store the metadata and the actual data separately in two
 * separate folders.
 */

typedef struct Cache Cache;

#include "link.h"
#include "network.h"

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

/**
 * \brief Type definition for a cache segment
 */
typedef uint8_t Seg;

/**
 * \brief cache data type in-memory data structure
 */
struct Cache {
    /** \brief How many times the cache has been opened */
    int cache_opened;

    /** \brief the FILE pointer for the data file*/
    FILE *dfp;
    /** \brief the FILE pointer for the metadata */
    FILE *mfp;
    /** \brief the path to the local cache file */
    char *path;
    /** \brief the Link associated with this cache data set */
    Link *link;
    /** \brief the modified time of the file */
    long time;
    /** \brief the size of the file */
    off_t content_length;
    /** \brief the block size of the data file */
    int blksz;
    /** \brief segment array byte count */
    long segbc;
    /** \brief the detail of each segment */
    Seg *seg;

    /** \brief mutex lock for seek operation */
    pthread_mutex_t seek_lock;
    /** \brief mutex lock for write operation */
    pthread_mutex_t w_lock;

    /** \brief background download pthread */
    pthread_t bgt;
    /**
     * \brief mutex lock for the background download thread
     * \note This lock is locked by the foreground thread, but unlocked by the
     * background thread!
     */
    pthread_mutex_t bgt_lock;
    /** \brief mutex attributes for bgt_lock */
    pthread_mutexattr_t bgt_lock_attr;
    /** \brief the offset of the next segment to be downloaded in background*/
    off_t next_dl_offset;

    /** \brief the FUSE filesystem path to the remote file*/
    char *fs_path;

    /** Transfer struct for the streaming cache */
    TransferStruct *ts;
};

/**
 * \brief whether the cache system is enabled
 */
extern int CACHE_SYSTEM_INIT;

/**
 * \brief The metadata directory
 */
extern char *META_DIR;

/**
 * \brief initialise the cache system directories
 * \details This function basically sets up the following variables:
 *  - META_DIR
 *  - DATA_DIR
 *
 * If these directories do not exist, they will be created.
 * \note Called by parse_arg_list(), verified to be working
 */
void CacheSystem_init(const char *path, int url_supplied);

/**
 * \brief Create directories under the cache directory structure, if they do
 * not already exist
 * \return
 *  -   -1 failed to create metadata directory.
 *  -   -2 failed to create data directory.
 *  -   -3 failed to create both metadata and data directory.
 * \note Called by LinkTable_new()
 */
int CacheDir_create(const char *fn);

/**
 * \brief open a cache file set
 * \note This function is called by fs_open()
 */
Cache *Cache_open(const char *fn);

/**
 * \brief Close a cache data structure
 * \note This function is called by fs_release()
 */
void Cache_close(Cache *cf);

/**
 * \brief create a cache file set if it doesn't exist already
 * \return
 *  -   0, if the cache file already exists, or was created successfully.
 *  -   -1, otherwise
 * \note Called by fs_open()
 */
int Cache_create(const char *path);

/**
 * \brief delete a cache file set
 * \note Called by fs_open()
 */
void Cache_delete(const char *fn);

/**
 * \brief Intelligently read from the cache system
 * \details If the segment does not exist on the local hard disk, download from
 * the Internet
 * \param[in] cf the cache in-memory data structure
 * \param[out] output_buf the output buffer
 * \param[in] len the requested segment size
 * \param[in] offset_start the start of the segment
 * \return the length of the segment the cache system managed to obtain.
 * \note Called by fs_read(), verified to be working
 */
long Cache_read(Cache *cf, char *const output_buf, const off_t len,
                const off_t offset_start);
#endif
