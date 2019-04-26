#ifndef LINK_H
#define LINK_H

#include "util.h"

#include <curl/curl.h>

/** \brief the link type */
typedef enum {
    LINK_HEAD = 'H',
    LINK_DIR = 'D',
    LINK_FILE = 'F',
    LINK_INVALID = '\0'
} LinkType;

/**
 * \brief link table type
 * \details index 0 contains the Link for the base URL
 */
typedef struct LinkTable LinkTable;

/** \brief link data type */
typedef struct Link Link;

/**
 * \brief Link data structure
 */
struct Link {
    char linkname[MAX_FILENAME_LEN]; /**< The link name in the last level of
                                            the URL */
    char f_url[MAX_PATH_LEN]; /**< The full URL of the file */
    LinkType type; /**< The type of the link */
    size_t content_length; /**< CURLINFO_CONTENT_LENGTH_DOWNLOAD of the file */
    LinkTable *next_table; /**< The next LinkTable level, if it is a LINK_DIR */
    long time; /**< CURLINFO_FILETIME obtained from the server */
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
 * \brief
 */
void Link_get_stat(Link *this_link);

/**
 * \brief set the stats for a file
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
