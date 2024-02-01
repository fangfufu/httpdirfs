#ifndef memcache_H
#define memcache_H
#include "link.h"

/**
 * \brief specify the type of data transfer
 */
typedef enum { FILESTAT = 's', DATA = 'd' } TransferType;

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
};

/**
 * \brief Callback function for file transfer
 */
size_t write_memory_callback(void *contents, size_t size, size_t nmemb,
                             void *userp);

#endif