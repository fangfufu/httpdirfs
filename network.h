#ifndef NETWORK_H
#define NETWORK_H

#include "link.h"

#define NETWORK_MAX_CONNS 20

typedef struct {
    char *memory;
    size_t size;
} MemoryStruct;

typedef enum {
    FILESTAT = 's',
    DATA = 'd'
} TransferType;

typedef struct {
    TransferType type;
    int transferring;
    Link *link;
} TransferStruct;

typedef struct {
    char *username;
    char *password;
    char *proxy_url;
    char *proxy_username;
    char *proxy_password;
    long max_conns;
} NetworkConfigStruct;

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
