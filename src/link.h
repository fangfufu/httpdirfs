#ifndef LINK_H
#define LINK_H

/**
 * \file link.h
 * \brief link related structures and functions
 */
#include "config.h"
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
    LINK_INVALID = 'I',
    LINK_UNINITIALISED_FILE = 'U'
} LinkType;

/** \brief specify the type of data transfer */
typedef enum {
    FILESTAT = 's',
    DATA = 'd'
} TransferType;

/** \brief For storing transfer data */
typedef struct {
    /** \brief The array to store the data */
    char *data;
    /** \brief The size of the array */
    size_t size;
    /** \brief The minium requested size */
    size_t min_req_size;
    /** \brief The type of transfer being done */
    TransferType type;
    /** \brief Whether transfer is in progress */
    int transferring;
    /** \brief The link associated with the transfer */
    Link *link;
} TransferStruct;

/**
 * \brief link table type
 * \details index 0 contains the Link for the base URL
 */
typedef struct LinkTable LinkTable;

/**
 * \brief Link type data structure
 */
struct Link {
    /** \brief The link name in the last level of the URL */
    char linkname[MAX_FILENAME_LEN + 1];
    /** \brief The full URL of the file */
    char f_url[MAX_PATH_LEN + 1];
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
    /**
     * \brief Sonic id field
     * \details This is used to store the followings:
     *  - Arist ID
     *  - Album ID
     *  - Song ID
     *  - Sub-directory ID (in the XML response, this is the ID on the "child"
     *    element)
     */
    char *sonic_id;
    /**
     * \brief Sonic directory depth
     * \details This is used exclusively in ID3 mode to store the depth of the
     * current directory.
     */
    int sonic_depth;
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
LinkTable *LinkSystem_init(const char *raw_url);

/**
 * \brief Set the stats of a link, after curl multi handle finished querying
 */
void Link_set_file_stat(Link * this_link, CURL * curl);

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
int LinkTable_disk_save(LinkTable * linktbl, const char *dirn);

/**
 * \brief load a link table from the disk.
 */
LinkTable *LinkTable_disk_open(const char *dirn);

/**
 * \brief Download a link's content to the memory
 * \warning You MUST free the memory field in TransferStruct after use!
 */
TransferStruct Link_to_TransferStruct(Link * head_link);

/**
 * \brief Allocate a LinkTable
 * \note This does not fill in the LinkTable.
 */
LinkTable *LinkTable_alloc(const char *url);

/**
 * \brief free a LinkTable
 */
void LinkTable_free(LinkTable * linktbl);

/**
 * \brief print a LinkTable
 */
void LinkTable_print(LinkTable * linktbl);

/**
 * \brief add a Link to a LinkTable
 */
void LinkTable_add(LinkTable * linktbl, Link * link);
#endif
