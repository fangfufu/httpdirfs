#ifndef DATA_H
#define DATA_H
/**
 * \file data.h
 * \brief This header stores all the custom data type definition
 */

#include <curl/curl.h>

#define URL_LEN_MAX 2048
#define LINK_LEN_MAX 255

/** \brief the link type */
typedef enum {
    LINK_DIR = 'D',
    LINK_FILE = 'F',
    LINK_UNKNOWN = 'U'
} LinkType;

/** \brief link data type */
typedef struct {
    char p_url[255];
    LinkType type;
    CURL *curl_h;
    CURLcode res;   /* initialise to -1, because all CURLcode are positive  */
    char *data;
    size_t data_sz;
    curl_off_t content_length;
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
