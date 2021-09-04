#ifndef RAMCACHE_H
#define RAMCACHE_H
#include "link.h"

/**
 * \brief specify the type of data transfer
 */
typedef enum {
    FILESTAT = 's',
    DATA = 'd'
} TransferType;

/**
 * \brief For storing transfer data and metadata
 */
struct TransferStruct {
    /** \brief The array to store the data */
    char *data;
    /** \brief The current size of the array */
    size_t curr_size;
    /** \brief The type of transfer being done */
    TransferType type;
    /** \brief Whether transfer is in progress */
    volatile int transferring;
    /** \brief The link associated with the transfer */
    Link *link;
    /** \brief The minium requested size */
    size_t min_req_size;
    /** \brief mutex for background transfer */
    pthread_mutex_t lock;
    /** \brief attribute associated with the mutex */
    pthread_mutexattr_t lock_attr;
    /** \brief Whether this TransferStruct was used for background transfer */
    int bg_transfer;
    /** \brief The cache file used for background transfer */
    Cache *cf;
    /** \brief The ID of the segment being downloaded */
    off_t seg_id;
};

/**
 * \brief Callback function for file transfer
 */
size_t write_memory_callback(void *contents, size_t size, size_t nmemb,
                             void *userp);

/**
 * \brief Create a TransferStruct for background transfer
 */
TransferStruct *TransferStruct_bg_transfer_new();

/**
 * \brief Free a TransferStruct used for background transfer
 */
void TransferStruct_free(TransferStruct *ts);

#endif