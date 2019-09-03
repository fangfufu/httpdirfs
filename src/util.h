#ifndef UTIL_H
#define UTIL_H
/**
 * \file util.h
 * \brief utility functions
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

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

/**
 * \brief wrapper for pthread_mutex_lock()
 */
void PTHREAD_MUTEX_LOCK(pthread_mutex_t *x);

/**
 * \brief wrapper for pthread_mutex_unlock()
 */
void PTHREAD_MUTEX_UNLOCK(pthread_mutex_t *x);

/**
 * \brief wrapper for exit(EXIT_FAILURE)
 */
void exit_failure(void);


/**
 * \brief erase a string from the terminal
 */
void erase_string(FILE *file, size_t max_len, char *s);

#endif
