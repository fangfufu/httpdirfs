#ifndef UTIL_H
#define UTIL_H
/**
 * \file util.h
 * \brief utility functions
 */

#include <stdint.h>
#include <stdlib.h>

#define PTHREAD_MUTEX_UNLOCK(x)\
({\
    int i;\
    i = pthread_mutex_unlock(x);\
    if (i) { \
        fprintf(stderr, "pthread_mutex_unlock failed, %d, %s\n", i, \
        strerror(i));\
        exit(EXIT_FAILURE);\
    }\
})

#define PTHREAD_MUTEX_LOCK(x)\
({\
    int i;\
    i = pthread_mutex_lock(x);\
    if (i) { \
        fprintf(stderr, "pthread_mutex_lock failed, %d, %s\n", i, \
        strerror(i));\
        exit(EXIT_FAILURE);\
    }\
})

/**
 * \brief the maximum length of a path and a URL.
 * \details This corresponds the maximum path length under Ext4.
 */
#define MAX_PATH_LEN        4096

/** \brief the maximum length of a filename. */
#define MAX_FILENAME_LEN    255

/**
 * \brief append a path
 * \details This function appends a path with the next level, while taking the
 * trailing slash of the upper level into account.
 *
 * Please free the char * after use.
 */
char *path_append(const char *path, const char *filename);

/**
 * \brief division, but rounded to the nearest integer rather than truncating
 */
int64_t round_div(int64_t a, int64_t b);


#endif
