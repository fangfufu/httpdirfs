#ifndef CACHE_H
#define CACHE_H
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include "util.h"

typedef struct Cache Cache;
typedef struct Link Link;
struct TransferStruct;


typedef struct ActiveDownload {
    off_t offset;
    struct TransferStruct *ts;
    pthread_cond_t cond;
    /**
     * \brief Reference count for lifetime management.
     * \details Starts at 1 when added to cf->active_dls.
     * Each waiter thread increments the reference count before waiting.
     * When unlinked from the list in ActiveDownload_remove, the list's
     * reference is dropped. The structure is freed only when the reference
     * count reaches 0.
     */
    int refcount;
    int unlinked;
    struct ActiveDownload *next;
} ActiveDownload;


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

    /** \brief semaphore for the background thread */
    sys_sem_t bgt_sem;

    /** \brief mutex lock for background download progress */
    pthread_mutex_t dl_lock;
    /** \brief active downloads list */
    ActiveDownload *active_dls;

    /** \brief Count of active waiters on download segments */
    int waiters;
    /** \brief Condition variable for cache shutdown synchronization */
    pthread_cond_t shutdown_cond;
    /** \brief Flag indicating that cache shutdown is in progress */
    int shutting_down;

    /** \brief Number of background workers */
    int num_bg_workers;
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
 * \brief clean up the cache system, freeing meta and data directories
 */
void CacheSystem_cleanup(void);

/**
 * \brief clear the content of the cache directory
 */
void CacheSystem_clear(void);

/**
 * \brief Return the fullpath to the cache directory
 */
char *CacheSystem_get_cache_dir(void);

/**
 * \brief Create directories under the cache directory structure, if they do
 * not already exist
 * \return
 *  -   -1 failed to create metadata directory.
 *  -   -2 failed to create data directory.
 *  -   -3 failed to create both metadata and data directory.
 * \note Called by LinkTable_new()
 */
int CacheDir_create(const char *dirn);

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
long Cache_read(Cache *cf, char *output_buf, off_t len, off_t offset_start);

/**
 * \brief Searches the active downloads linked list for a matching offset.
 * \param[in] cf The cache instance.
 * \param[in] offset The offset to search for.
 * \return The active download structure if found, otherwise NULL.
 * \note Must be called while holding cf->dl_lock.
 */
ActiveDownload *ActiveDownload_find(Cache *cf, off_t offset);

/**
 * \brief Decrements the reference count of the ActiveDownload tracker.
 * \details Destroys the condition variable and frees the structure when the
 * reference count reaches 0.
 * \param[in] ad The ActiveDownload tracker to unref.
 * \note Must be called while holding cf->dl_lock.
 */
void ActiveDownload_unref(ActiveDownload *ad);
#endif
