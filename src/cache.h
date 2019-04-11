#ifndef CACHE_H
#define CACHE_H

#include <unistd.h>

/**
 * \file cache.h
 * \brief cache related structures and functions
 * \details think of these as some sort of generic caching layer.
 * \note apologies for whoever is going to be reading this. Sorry for being so
 * verbose in this header file, this is probably one of the most challenging
 * thing I have ever written so far! Yes I am doing a PhD in computer science,
 * but it doesn't imply that I am good at computer science or programming!
 */

/**
 * \brief cache metadata structure
 * \details we use linked list to store the information about fragments
 * \note fanf2@cam.ac.uk told me to use an array rather than linked list!
 */
typedef struct CacheFile CacheFile;

struct CacheFile {
    FILE *file_fd;
    FIILE *meta_fd;
    int seg_count;
    off_t *start;
    off_t *end;
};

/**
 * \brief create cache file
 */
int CacheFile_create(const char *filepath, size_t size);

/**
 * \brief read cache file
 */
size_t CacheFile_read(const char *filepath, off_t offset, size_t size);

/**
 * \brief write cache file
 */
size_t CacheFile_write(const char *filepath, off_t offset, size_t size,
                    uint8_t, *content);

#endif
