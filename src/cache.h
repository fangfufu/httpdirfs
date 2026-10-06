/*
 * HTTPDirFS - HTTP Directory Filesystem
 *
 * Copyright (C) 2020-2026 Fufu Fang <fangfufu2003@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the OpenSSL
 * library.
 */

#ifndef CACHE_H
#define CACHE_H
/**
 * \file cache.h
 * \brief Permanent cache system header
 */

#include <curl/curl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include "link.h"
#include "util.h"

typedef struct Cache Cache;
typedef struct Link Link;
struct TransferStruct;

/**
 * \brief Magic number identifying a cache container file ("HDFS", LE)
 */
#define CACHE_MAGIC 0x53464448u

/**
 * \brief Container format version
 */
#define CACHE_VERSION 1

/**
 * \brief flags bitmask: payload is fully downloaded
 */
#define CACHE_FLAG_IS_COMPLETE 0x1

/**
 * \brief flags bitmask: payload region was sparse-allocated
 */
#define CACHE_FLAG_IS_SPARSE 0x2

/**
 * \brief flags bitmask: container contains cached HEAD / stat metadata
 */
#define CACHE_FLAG_IS_HEAD 0x4

/**
 * \brief flags bitmask: classified as a directory (text/html)
 */
#define CACHE_FLAG_IS_DIR 0x8

/**
 * \brief flags bitmask: container is a redirect pointer to target URL
 */
#define CACHE_FLAG_IS_REDIRECT 0x10

/**
 * \brief Cached file stat metadata returned by CacheContainer_read_head()
 */
typedef struct CacheStat {
    long http_resp;            /**< HTTP status code (e.g. 200) */
    curl_off_t content_length; /**< Remote content length */
    time_t remote_mtime;       /**< Remote Last-Modified timestamp */
    char content_type[128];    /**< Content-Type MIME string */
    LinkType link_type;        /**< LINK_DIR, LINK_FILE, or LINK_INVALID */
} CacheStat;

/**
 * \brief Size in bytes of the fixed-size CacheHeader
 */
#define CACHE_HEADER_SIZE 64

/**
 * \brief Page size used to align the payload within a container file
 */
#define CACHE_PAGE_SIZE 4096

/**
 * \brief Fixed-size binary header stored at the start of every cache
 * container file.
 */
typedef struct CacheHeader {
    uint32_t magic;           /**< CACHE_MAGIC */
    uint16_t version;         /**< CACHE_VERSION */
    uint16_t flags;           /**< Bitmask (CACHE_FLAG_*) */
    uint32_t url_len;         /**< Length of canonical source URL string */
    uint32_t header_size;     /**< Offset where the payload begins (4KB
                                  aligned) */
    uint32_t http_header_len; /**< Length of raw HTTP response headers */
    int64_t cache_time;       /**< Local time(NULL) when downloaded */
    int64_t remote_mtime;     /**< Remote server Last-Modified (or 0) */
    int64_t content_length;   /**< Total length of payload in bytes */
    int32_t blksz;            /**< Segment block size */
    int32_t segbc;           /**< Total segment count (1 for non-sparse HTML) */
    uint8_t reserved[4];     /**< Reserved for future use (zero) */
    int64_t head_cache_time; /**< Local time when HEAD metadata was refreshed
                                 (0 for pre-existing headers; fall back to
                                 cache_time) */
} __attribute__((packed)) CacheHeader;

_Static_assert(sizeof(CacheHeader) == CACHE_HEADER_SIZE,
               "CacheHeader size must be CACHE_HEADER_SIZE");


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

    /** \brief the FILE pointer for the single cache container file */
    FILE *fp;
    /** \brief the path to the local cache file (relative to CACHE_DIR) */
    char *path;
    /** \brief the Link associated with this cache data set */
    Link *link;
    /** \brief the block size of the data file */
    int blksz;
    /** \brief segment array byte count */
    long segbc;
    /** \brief the detail of each segment */
    Seg *seg;

    /** \brief offset where the payload begins (CACHE_PAGE_SIZE aligned) */
    off_t header_size;
    /** \brief offset of the segment bitmap within the container file */
    off_t bitmap_offset;
    /** \brief offset of the raw HTTP response headers */
    off_t http_header_offset;

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
 * \brief The cache root directory of the mounted server (the
 * escaped-server-root directory that contains the "ab/" shard directories).
 */
extern char *CACHE_DIR;

/**
 * \brief Return the cache root directory of a server origin.
 * \details Returns "<cache location>" if CONFIG.cache_dir is set, or the
 * default "<cache home>/httpdirfs" root.
 * \note The caller must free the returned string with FREE().
 */
char *CacheSystem_get_cache_root(void);

/**
 * \brief Compute the cache directory of a server origin.
 * \details The cache root (a custom --cache-location if set, or the
 * default "<cache home>/httpdirfs") with the escaped server root of
 * \p url appended. Creates the root (with a CACHEDIR.TAG) and the
 * origin directory on disk.
 * \note The caller must free the returned string with FREE().
 */
char *CacheSystem_calc_dir(const char *url);

/**
 * \brief initialise the cache system directory
 * \details This function basically sets up the following variables:
 *  - CACHE_DIR
 *
 * CACHE_DIR is set to the cache directory of the mounted server origin
 * (the cache root with the escaped server root appended). If the
 * directory does not exist, it will be created.
 * \note Called by LinkSystem_init(), verified to be working
 */
void CacheSystem_init(const char *url);

/**
 * \brief clean up the cache system, freeing the cache directory
 */
void CacheSystem_cleanup(void);

/**
 * \brief clear the content of the cache directory
 */
void CacheSystem_clear(void);

/**
 * \brief delete the cache directory of a single server host
 * \details If \p arg contains "://", the server root is extracted and only
 * "<cache root>/<escaped server root>/" is removed. Otherwise both the
 * "http" and "https" origin directories of the bare host are removed.
 * \return 1 if at least one host directory was found, 0 if none, -1 on
 * invalid argument.
 */
int CacheSystem_delete_host(const char *arg);

/**
 * \brief delete the cache of a single server host, then exit
 * \note Called by parse_arg_list() for --cache-clear-host
 */
void CacheSystem_clear_host(const char *arg);

/**
 * \brief Return the fullpath to the user's base cache directory
 * \note The caller must free the returned string with FREE().
 */
char *CacheSystem_get_cache_dir(void);

/**
 * \brief Write a fresh container file for a cached directory listing.
 * \details Stores the raw HTTP response headers and the HTML payload in a
 * single container file at "<CACHE_DIR>/<shard>/<hash>", using the CacheHeader
 * binary format.
 * \return 0 on success, -1 on failure
 * \note Called by LinkTable_new()
 */
int CacheContainer_write(const char *url, const char *payload,
                         size_t payload_len, const char *http_header,
                         size_t http_header_len);

/**
 * \brief Delete the cached container file of a URL, if present.
 * \param[in] url Resource URL.
 * \return 0 on success (including when nothing was cached), -1 on error
 * \note Called by LinkTable_new() when a cached directory listing now exceeds
 * max_html_size (the limit was lowered after caching), to drop the
 * now-inconsistent container before the entry degrades to an empty folder.
 */
int CacheContainer_delete(const char *url);

/**
 * \brief Read a cached container file for a URL, if it is still fresh.
 * \details A container is fresh if it is well-formed (magic, version, URL
 * and size checks) and
 * `time(NULL) - cache_time <= CONFIG.refresh_timeout`.
 * \param[out] out_payload malloc'ed payload buffer (set on success)
 * \param[out] out_payload_len length of the payload
 * \param[out] out_http_header malloc'ed raw HTTP headers (set on success)
 * \param[out] out_http_header_len length of the raw HTTP headers
 * \return
 *  -   1, loaded from the cache (caller frees the output buffers)
 *  -   0, not cached, or expired (nothing is freed)
 *  -   -1, the container was corrupt (it has been deleted)
 */
int CacheContainer_read(const char *url, char **out_payload,
                        size_t *out_payload_len, char **out_http_header,
                        size_t *out_http_header_len);

/**
 * \brief Read a cached container file for a URL, if it is still fresh, and
 * return its creation cache_time.
 * \details A container is fresh if it is well-formed (magic, version, URL
 * and size checks) and
 * `time(NULL) - cache_time <= CONFIG.refresh_timeout`.
 * \param[in] url Resource URL.
 * \param[out] out_payload malloc'ed payload buffer (set on success)
 * \param[out] out_payload_len length of the payload
 * \param[out] out_http_header malloc'ed raw HTTP headers (set on success)
 * \param[out] out_http_header_len length of the raw HTTP headers
 * \param[out] out_cache_time Optional pointer to receive container cache_time
 * \param[out] out_resolved_url Optional pointer to receive the effective URL
 * the payload was cached under (after following any redirect container),
 * malloc'ed; NULL if unset or on failure
 * \return
 *  -   1, loaded from the cache (caller frees the output buffers)
 *  -   0, not cached, or expired (nothing is freed)
 *  -   -1, the container was corrupt (it has been deleted)
 */
int CacheContainer_read_with_time(const char *url, char **out_payload,
                                  size_t *out_payload_len,
                                  char **out_http_header,
                                  size_t *out_http_header_len,
                                  time_t *out_cache_time,
                                  char **out_resolved_url);

/**
 * \brief Write an HTTP HEAD response to the URL's container file.
 * \param[in] url Resource URL.
 * \param[in] http_resp HTTP response status code (e.g. 200).
 * \param[in] content_length Remote content length.
 * \param[in] remote_mtime Remote server Last-Modified timestamp.
 * \param[in] content_type Remote Content-Type MIME string.
 * \param[in] raw_headers Optional raw response headers.
 * \param[in] raw_headers_len Length of raw response headers.
 * \param[in] link_type LINK_DIR or LINK_FILE.
 * \return 0 on success, -1 on error.
 */
int CacheContainer_write_head(const char *url, long http_resp,
                              curl_off_t content_length, time_t remote_mtime,
                              const char *content_type, const char *raw_headers,
                              size_t raw_headers_len, LinkType link_type);

/**
 * \brief Read cached HTTP HEAD / stat metadata for a URL if fresh.
 * \param[in] url Resource URL.
 * \param[out] stat_out Output struct filled with cached metadata.
 * \return 1 if found and fresh, 0 if not cached or expired, -1 on corrupt.
 */
int CacheContainer_read_head(const char *url, CacheStat *stat_out);

/**
 * \brief Write a redirect pointer container pointing to target_url.
 * \param[in] source_url The requested URL that resulted in a redirect.
 * \param[in] target_url The canonical target URL.
 * \param[in] http_resp HTTP redirect status code (e.g. 301, 302, 307, 308).
 * \return 0 on success, -1 on error.
 */
int CacheContainer_write_redirect(const char *source_url,
                                  const char *target_url, long http_resp);

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
