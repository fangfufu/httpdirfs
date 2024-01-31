#ifndef NETWORK_H
#define NETWORK_H

/**
 * \file network.h
 * \brief network related functions
 */

typedef struct TransferStruct TransferStruct;

#include "link.h"

#include <curl/curl.h>

/** \brief HTTP response codes */
typedef enum {
  HTTP_OK = 200,
  HTTP_PARTIAL_CONTENT = 206,
  HTTP_RANGE_NOT_SATISFIABLE = 416,
  HTTP_TOO_MANY_REQUESTS = 429,
  HTTP_CLOUDFLARE_UNKNOWN_ERROR = 520,
  HTTP_CLOUDFLARE_TIMEOUT = 524
} HTTPResponseCode;

/** \brief curl shared interface */
extern CURLSH *CURL_SHARE;

/** \brief perform one transfer cycle */
int curl_multi_perform_once(void);

/** \brief initialise the network module */
void NetworkSystem_init(void);

/** \brief blocking file transfer */
void transfer_blocking(CURL *curl);

/** \brief non blocking file transfer */
void transfer_nonblocking(CURL *curl);

/**
 * \brief check if a HTTP response code corresponds to a temporary failure
 */
int HTTP_temp_failure(HTTPResponseCode http_resp);

#endif
