#ifndef DATA_H
#define DATA_H
/**
 * \file data.h
 * \brief This header stores all the custom data type definition
 */

#include <curl/curl.h>

#define URL_LEN_MAX 2048
#define LINK_LEN_MAX 255
#define HTTP_OK 200

/** \brief the link type */
typedef enum {
    LINK_HEAD = 'H',
    LINK_DIR = 'D',
    LINK_FILE = 'F',
    LINK_UNKNOWN = 'U',
    LINK_INVALID = 'I'
} LinkType;

/** \brief link data type */
typedef struct {
    char p_url[255];
    LinkType type;
    CURL *curl;
    CURLcode res;   /* initialise to -1, as all valid CURLcode are positive  */
    char *body;
    size_t body_sz;
    size_t content_length;
} Link;

/**
 * \brief link table type
 * \details index 0 contains the Link for the base URL
 */
typedef struct {
    int num;
    Link **links;
} LinkTable;

#endif
