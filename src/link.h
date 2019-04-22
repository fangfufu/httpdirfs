#ifndef LINK_H
#define LINK_H

#include <curl/curl.h>

#include <stdlib.h>

/** \brief the maximum length of the URL */
#define URL_LEN_MAX 2048
/** \brief the maximum length of a partial URL (a link) */
#define LINKNAME_LEN_MAX 255

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
    char linkname[LINKNAME_LEN_MAX]; /**< The link name in the last level of
                                            the URL*/
    char f_url[URL_LEN_MAX]; /**< The full URL of the file*/
    LinkType type; /**< The type of the link */
    size_t content_length; /**< CURLINFO_CONTENT_LENGTH_DOWNLOAD of the file */
    LinkTable *next_table; /**< The next LinkTable level, if it is a LINK_DIR */
    long time; /**< CURLINFO_FILETIME obtained from the server*/
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
 * \brief the length of the root link
 */
extern int ROOT_LINK_LEN;

/**
 * \brief set the stats for a file
 */
void Link_set_stat(Link* this_link, CURL *curl);

/**
 * \brief create a new LinkTable
 */
LinkTable *LinkTable_new(const char *url);

/**
 * \brief print a LinkTable
 */
void LinkTable_print(LinkTable *linktbl);

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


#endif
