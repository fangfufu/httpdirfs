#ifndef NETWORK_H
#define NETWORK_H

#include "link.h"

#define CURL_MULTI_MAX_CONNECTION 20

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

/** \brief curl shared interface */
extern CURLSH *curl_share;

/** \brief perform one transfer cycle */
int curl_multi_perform_once();

/** \brief Initialise the network module */
void network_init(const char *url);

/** \brief blocking file transfer */
void transfer_blocking(CURL *curl);

/** \brief non blocking file transfer */
void transfer_nonblocking(CURL *curl);

/** \brief callback function for file transfer */
size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);

#endif
