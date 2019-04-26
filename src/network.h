#ifndef NETWORK_H
#define NETWORK_H

#include "link.h"

typedef enum {
    HTTP_OK                     = 200,
    HTTP_PARTIAL_CONTENT        = 206,
    HTTP_RANGE_NOT_SATISFIABLE  = 416,
    HTTP_TOO_MANY_REQUESTS      = 429
}HTTPResponseCode;

typedef enum {
    FILESTAT = 's',
    DATA = 'd'
} TransferType;

typedef struct {
    char *memory;
    size_t size;
} MemoryStruct;

typedef struct {
    TransferType type;
    int transferring;
    Link *link;
} TransferStruct;

typedef struct {
    char *username;
    char *password;
    char *proxy;
    char *proxy_user;
    char *proxy_pass;
    long max_conns;
    char *user_agent;
    int http_429_wait;
} NetworkConfigStruct;

/** \brief The waiting time after getting HTTP 429 */
extern int HTTP_429_WAIT;

/** \brief CURL configuration */
extern NetworkConfigStruct NETWORK_CONFIG;

/** \brief curl shared interface */
extern CURLSH *CURL_SHARE;

/** \brief perform one transfer cycle */
int curl_multi_perform_once();

/** \brief initialise network config struct */
void network_config_init();

/** \brief initialise the network module */
LinkTable *network_init(const char *url);

/** \brief blocking file transfer */
void transfer_blocking(CURL *curl);

/** \brief non blocking file transfer */
void transfer_nonblocking(CURL *curl);

/** \brief callback function for file transfer */
size_t
write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp);

#endif
