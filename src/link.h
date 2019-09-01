#ifndef LINK_H
#define LINK_H

/**
 * \file link.h
 * \brief link related structures and functions
 */

#include "util.h"

#include <curl/curl.h>

/** \brief Link type */
typedef struct Link Link;

#include "cache.h"

/** \brief the link type */
typedef enum {
    LINK_HEAD = 'H',
    LINK_DIR = 'D',
    LINK_FILE = 'F',
    LINK_INVALID = 'I'
} LinkType;

/**
 * \brief link table type
 * \details index 0 contains the Link for the base URL
 */
typedef struct LinkTable LinkTable;

/**
 * \brief Link type data structure
 */
struct Link {
    char linkname[MAX_FILENAME_LEN+1]; /**< The link name in the last level of
                                            the URL */
    char f_url[MAX_PATH_LEN+1]; /**< The full URL of the file */
    LinkType type; /**< The type of the link */
    size_t content_length; /**< CURLINFO_CONTENT_LENGTH_DOWNLOAD of the file */
    LinkTable *next_table; /**< The next LinkTable level, if it is a LINK_DIR */
    long time; /**< CURLINFO_FILETIME obtained from the server */
    int cache_opened; /**< How many times associated cache has been opened */
    Cache *cache_ptr; /**< The pointer associated with the cache file */
};

struct LinkTable {
    int num;
    Link **links;
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
void link_system_init();

/**
 * \brief Add a link to the curl multi bundle for querying stats
 */
void Link_get_stat(Link *this_link);

/**
 * \brief Set the stats of a link, after curl multi handle finished querying
 */
void Link_set_stat(Link* this_link, CURL *curl);

/**
 * \brief create a new LinkTable
 */
LinkTable *LinkTable_new(const char *url);

/**
 * \brief download a link
 * \return the number of bytes downloaded
 */
long path_download(const char *path, char *output_buf, size_t size,
                   off_t offset);

/**
 * \brief find the link associated with a path
 */
Link *path_to_Link(const char *path);

/**
 * \brief return the link table for the associated path
 */
LinkTable *path_to_Link_LinkTable_new(const char *path);

/**
 * \brief dump a link table to the disk.
 */
int LinkTable_disk_save(LinkTable *linktbl, const char *dirn);

/**
 * \brief load a link table from the disk.
 */
LinkTable *LinkTable_disk_open(const char *dirn);
#endif
