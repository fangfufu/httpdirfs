#ifndef NETWORK_H
#define NETWORK_H

#include "data.h"

#include <curl/curl.h>
#include <gumbo.h>

#define URL_LEN_MAX 2048
#define LINK_LEN_MAX 255
#define NETWORK_MAXIMUM_CONNECTION 10

/** \brief the link type */
typedef enum {
    LINK_HEAD = 'H',
    LINK_DIR = 'D',
    LINK_FILE = 'F',
    LINK_UNKNOWN = 'U',
    LINK_INVALID = 'I'
} LinkType;

/**
 * \brief link table type
 * \details index 0 contains the Link for the base URL
 */
typedef struct LinkTable LinkTable;

/** \brief link data type */
typedef struct Link Link;


struct Link {
    char p_url[LINK_LEN_MAX];
    char f_url[URL_LEN_MAX];
    LinkType type;
    size_t content_length;
    LinkTable *next_table;
};

struct LinkTable {
    int num;
    Link **links;
};

/** \brief root link table */
extern LinkTable *ROOT_LINK_TBL;

/** \brief Initialise the network module */
void network_init(const char *url);

/**
 * \brief download a link */
/* \return the number of bytes downloaded
 */
long Link_download(const char *path, char *output_buf, size_t size,
                   off_t offset);

/** \brief create a new LinkTable */
LinkTable *LinkTable_new(const char *url);

/** \brief print a LinkTable */
void LinkTable_print(LinkTable *linktbl);

/** \brief find the link associated with a path */
Link *path_to_Link(const char *path);

#endif
