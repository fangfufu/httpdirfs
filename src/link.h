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

#ifndef LINK_H
#define LINK_H

/**
 * \file link.h
 * \brief Link structure and handling functions header
 */

#include <curl/curl.h>
#include <limits.h>
#include <sys/types.h>

#include "memcache.h"
#include "sonic.h"

typedef struct Cache Cache;
typedef struct Link Link;
typedef struct LinkTable LinkTable;


/**
 * \brief the link type
 */
typedef enum {
    LINK_HEAD = 'H',
    LINK_DIR = 'D',
    LINK_FILE = 'F',
    LINK_INVALID = 'I',
    LINK_UNINITIALISED_FILE = 'U',
    LINK_UNINITIALISED_DIR = 'V',
} LinkType;

/**
 * \brief link table type
 * \details index 0 contains the Link for the base URL
 */
struct LinkTable {
    int size;
    time_t index_time;
    Link **links;
    int refcount;
    int orphaned;
    struct LinkTable *parent_tbl;
    Link *parent_link;
};

/**
 * \brief Link type data structure
 */
struct Link {
    /** \brief The parent LinkTable of this link */
    struct LinkTable *parent_table;
    /** \brief The link name in the last level of the URL */
    char linkname[NAME_MAX + 1];
    /** \brief This is for storing the unescaped path */
    char linkpath[NAME_MAX + 1];
    /** \brief The full URL of the file */
    char f_url[PATH_MAX + 1];
    /** \brief The type of the link */
    LinkType type;
    /** \brief CURLINFO_CONTENT_LENGTH_DOWNLOAD of the file */
    size_t content_length;
    /** \brief The next LinkTable level, if it is a LINK_DIR */
    LinkTable *next_table;
    /** \brief CURLINFO_FILETIME obtained from the server */
    long time;
    /** \brief The pointer associated with the cache file */
    Cache *cache_ptr;
    /** \brief Stores *sonic related data */
    Sonic sonic;
};

/**
 * \brief root link table
 */
extern LinkTable *ROOT_LINK_TBL;

/**
 * \brief the offset for calculating partial URL
 */
extern int ROOT_LINK_OFFSET;

/**
 * \brief initialise link sub-system.
 */
LinkTable *LinkSystem_init(const char *raw_url);

/**
 * \brief Set the stats of a link, after curl multi handle finished querying
 */
void Link_set_file_stat(Link *this_link, CURL *curl);

/**
 * \brief create a new LinkTable
 */
LinkTable *LinkTable_new(const char *url);

/**
 * \brief download a path
 * \return the number of bytes downloaded
 */
long path_download(const char *path, char *output_buf, size_t size,
                   off_t offset);

/**
 * \brief Download a Link
 * \return the number of bytes downloaded
 */
long Link_download(Link *link, char *output_buf, size_t req_size, off_t offset,
                   Cache *cf);

/**
 * \brief find the link associated with a path
 */
Link *path_to_Link(const char *path);

/**
 * \brief return the link table for the associated path
 */
LinkTable *path_to_LinkTable(const char *path);

/**
 * \brief dump a link table to the disk.
 */
int LinkTable_disk_save(LinkTable *linktbl, const char *dirn);

/**
 * \brief load a link table from the disk.
 * \param[in] dirn We expected the unescaped_path here!
 */
LinkTable *LinkTable_disk_open(const char *dirn);

/**
 * \brief Download a link's content to the memory
 * \warning You MUST free the memory field in TransferStruct after use!
 */
TransferStruct Link_download_full(Link *head_link);

/**
 * \brief Allocate a LinkTable
 * \note This does not fill in the LinkTable.
 */
LinkTable *LinkTable_alloc(const char *url);

/**
 * \brief free a LinkTable
 */
void LinkTable_free(LinkTable *linktbl);

/**
 * \brief increment the reference count of a LinkTable
 */
void LinkTable_ref(LinkTable *tbl);

/**
 * \brief decrement the reference count of a LinkTable
 */
void LinkTable_unref(LinkTable *tbl);

/**
 * \brief mark a LinkTable as orphaned so it can be evicted
 */
void LinkTable_mark_orphaned(LinkTable *tbl);

/**
 * \brief print a LinkTable
 */
void LinkTable_print(LinkTable *linktbl);

/**
 * \brief add a Link to a LinkTable
 */
void LinkTable_add(LinkTable *linktbl, Link *link);

/**
 * \brief Parse HTML content and populate LinkTable with unique links.
 */
void LinkTable_parse_html(LinkTable *linktbl, const char *url,
                          const char *html);

/*
 * Functions exposed for unit testing duplicated URL logic
 */
typedef struct LinkHashSet LinkHashSet;

/**
 * \brief Check if two link names are equal, normalizing any single trailing
 * slash.
 * \param str_a The first link name string to compare.
 * \param str_b The second link name string to compare.
 * \return 1 if they are equivalent, 0 otherwise.
 */
int link_linknames_equal(const char *str_a, const char *str_b);

/**
 * \brief Generate a hash value for a link name, ignoring any trailing slashes.
 * \param str The link name string to hash.
 * \return The generated unsigned int hash value.
 */
unsigned int link_hash_str(const char *str);

/**
 * \brief Create a new LinkHashSet with a specified initial capacity.
 * \param capacity The initial number of buckets to allocate.
 * \return Pointer to the newly allocated LinkHashSet.
 */
LinkHashSet *LinkHashSet_new(int capacity);

/**
 * \brief Add a link name to the LinkHashSet if it is not already present.
 * \param set The LinkHashSet to insert the link name into.
 * \param linkname The link name string to add.
 * \return 1 if successfully added (not a duplicate), 0 if it is a duplicate.
 */
int LinkHashSet_add(LinkHashSet *set, const char *linkname);

/**
 * \brief Free all memory allocated for a LinkHashSet.
 * \param set The LinkHashSet to deallocate.
 */
void LinkHashSet_free(LinkHashSet *set);

/**
 * \brief Check if a URL is an external (absolute) http/https URL.
 * \return 1 if the URL starts with http:// or https://, 0 otherwise
 */
int is_external_url(const char *url);

/**
 * \brief Check if link_url has a different origin than page_url.
 * \details Compares scheme + host + port. Malformed URLs are treated as
 * cross-origin.
 * \return 1 if cross-origin or either URL is malformed, 0 if same origin
 */
int is_cross_origin(const char *page_url, const char *link_url);

/**
 * \brief Extract the filename component from an external URL.
 * \details For "http://example.com/path/file.iso" returns "file.iso".
 *          For "http://example.com/path/dir/" returns "dir".
 *          Query strings are stripped. Returns "" for root-only URLs.
 * \note The caller must free the returned string with FREE().
 */
char *external_url_to_filename(const char *url);

/**
 * \brief Safely generate the cache path for a given URL, handling cross-origin
 * external links.
 * \note The caller must free the returned string with FREE().
 */
char *url_to_cache_path(const char *url);
#endif
