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

/**
 * \brief the maximum length of a filename.
 * \details This corresponds the filename length under Ext4.
 */
#define MAX_FILENAME_LEN    255

/**
 * \brief the default user agent string
 */
#define DEFAULT_USER_AGENT "HTTPDirFS-" VERSION






/**
 * \brief configuration data structure
 * \note The opening curly bracket should be at line 39, so the code belong
 * lines up with the initialisation code in util.c
 */
typedef struct {
    /** \brief HTTP username */
    char *http_username;
    /** \brief HTTP password */
    char *http_password;
    /** \brief HTTP proxy URL */
    char *proxy;
    /** \brief HTTP proxy username */
    char *proxy_username;
    /** \brief HTTP proxy password */
    char *proxy_password;
    /** \brief HTTP maximum connection count */
    long max_conns;
    /** \brief HTTP user agent*/
    char *user_agent;
    /** \brief The waiting time after getting HTTP 429 (too many requests) */
    int http_wait_sec;

    /** \brief Whether cache mode is enabled */
    int cache_enabled;
    /** \brief The cache location*/
    char *cache_dir;
    /** \brief The size of each download segment for cache mode */
    int data_blksz;
    /** \brief The maximum segment count for a single cache file */
    int max_segbc;

    /** \brief Whether we are using the Subsonic mode */
    int sonic_mode;
    /** \brief The Subsonic server username */
    char *sonic_username;
    /** \brief The Subsonic server password */
    char *sonic_password;
} ConfigStruct;

/**
 * \brief The Configuration data structure
 */
extern ConfigStruct CONFIG;

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
 * \brief wrapper for pthread_mutex_lock(), with error handling
 */
void PTHREAD_MUTEX_LOCK(pthread_mutex_t *x);

/**
 * \brief wrapper for pthread_mutex_unlock(), with error handling
 */
void PTHREAD_MUTEX_UNLOCK(pthread_mutex_t *x);

/**
 * \brief wrapper for exit(EXIT_FAILURE), with error handling
 */
void exit_failure(void);

/**
 * \brief erase a string from the terminal
 */
void erase_string(FILE *file, size_t max_len, char *s);

/**
 * \brief generate the salt for authentication string
 * \details this effectively generates a UUID string, which we use as the salt
 * \return a pointer to a 37-char array with the salt.
 */
char *generate_salt();

/**
 * \brief generate the md5sum of a string
 * \param[in] str a character array for the input string
 * \return a pointer to a 33-char array with the salt
 */
char *generate_md5sum(const char *str);

/**
 * \brief wrapper for calloc(), with error handling
 */
void *CALLOC(size_t nmemb, size_t size);

/**
 * \brief initialise the configuration data structure
 */
void Config_init(void);

#endif
