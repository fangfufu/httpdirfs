#ifndef UTIL_H
#define UTIL_H
/**
 * \file util.h
 * \brief utility functions
 */

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>

/**
 * \brief append a path
 * \details This function appends a path with the next level, while taking the
 * trailing slash of the upper level into account.
 * \note You need to free the char * after use.
 */
char *path_append(const char *path, const char *filename);

/**
 * \brief division, but rounded to the nearest integer rather than truncating
 */
int64_t round_div(int64_t a, int64_t b);

/**
 * \brief wrapper for pthread_mutex_init(), with error handling
 */
void pthread_mutex_init_wrapper(pthread_mutex_t *x,
                                const pthread_mutexattr_t *attr,
                                const char *file, const char *func, int line,
                                const char *x_name);
#define PTHREAD_MUTEX_INIT(x, attr)                                            \
    pthread_mutex_init_wrapper(x, attr, __FILE__, __func__, __LINE__, #x)

/**
 * \brief wrapper for pthread_mutex_destroy(), with error handling
 */
void pthread_mutex_destroy_wrapper(pthread_mutex_t *x, const char *file,
                                   const char *func, int line,
                                   const char *x_name);
#define PTHREAD_MUTEX_DESTROY(x)                                               \
    pthread_mutex_destroy_wrapper(x, __FILE__, __func__, __LINE__, #x)

/**
 * \brief wrapper for pthread_mutex_lock(), with error handling
 */
void pthread_mutex_lock_wrapper(const char *file, const char *func, int line,
                                pthread_mutex_t *x, const char *x_name);
#define PTHREAD_MUTEX_LOCK(x)                                                  \
    pthread_mutex_lock_wrapper(__FILE__, __func__, __LINE__, x, #x)

/**
 * \brief wrapper for pthread_mutex_unlock(), with error handling
 */
void pthread_mutex_unlock_wrapper(const char *file, const char *func, int line,
                                  pthread_mutex_t *x, const char *x_name);
#define PTHREAD_MUTEX_UNLOCK(x)                                                \
    pthread_mutex_unlock_wrapper(__FILE__, __func__, __LINE__, x, #x)

/**
 * \brief wrapper for sem_init(), with error handling
 * */
void sem_init_wrapper(sem_t *sem, int pshared, unsigned int value,
                      const char *file, const char *func, int line,
                      const char *sem_name);
#define SEM_INIT(sem, pshared, value)                                          \
    sem_init_wrapper(sem, pshared, value, __FILE__, __func__, __LINE__, #sem)

/**
 * \brief wrapper for sem_destroy(), with error handling
 */
void sem_destroy_wrapper(sem_t *sem, const char *file, const char *func,
                         int line, const char *sem_name);
#define SEM_DESTROY(sem)                                                       \
    sem_destroy_wrapper(sem, __FILE__, __func__, __LINE__, #sem)

/**
 * \brief wrapper for sem_wait(), with error handling
 */
void sem_wait_wrapper(const char *file, const char *func, int line, sem_t *sem,
                      const char *sem_name);
#define SEM_WAIT(sem) sem_wait_wrapper(__FILE__, __func__, __LINE__, sem, #sem)

/**
 * \brief wrapper for sem_post(), with error handling
 */
void sem_post_wrapper(const char *file, const char *func, int line, sem_t *sem,
                      const char *sem_name);
#define SEM_POST(sem) sem_post_wrapper(__FILE__, __func__, __LINE__, sem, #sem)

/**
 * \brief wrapper for exit(EXIT_FAILURE), with error handling
 */
void exit_failure(void) __attribute__((noreturn));

/**
 * \brief erase a string from the terminal
 */
void erase_string(FILE *file, size_t max_len, char *s);

/**
 * \brief generate the salt for authentication string
 * \details this effectively generates a UUID string, which we use as the salt
 * \return a pointer to a 37-char array with the salt.
 */
char *generate_salt(void);

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
 * \brief Internal wrapper for free().
 */
void FREE_wrapper(void *ptr);

/**
 * \brief Macro wrapper for free() that sets the pointer to NULL afterwards.
 * \note This macro is safe for pointers to const data (e.g., const char *p)
 * because it only modifies the pointer variable itself, not the data it points
 * to. However, it will fail for const-qualified pointer variables (e.g., char *
 * const p) as it attempts to set the pointer itself to NULL.
 * \note When used on a function parameter (e.g., FREE(ptr)), it only nullifies
 * the local copy of the pointer within the function scope. The caller's
 * original pointer remains unchanged and may become a dangling pointer.
 */
#define FREE(ptr)                                                              \
    do {                                                                       \
        __extension__ __auto_type __ptr_to_free = &(ptr);                      \
        FREE_wrapper((void *)*__ptr_to_free);                                  \
        *__ptr_to_free = NULL;                                                 \
    } while (0)

/**
 * \brief Convert a string to hex
 */
char *str_to_hex(char *s);

/**
 * \brief wrapper for strdup(), with error handling
 */
char *STRDUP(const char *s);

/**
 * \brief wrapper for strndup(), with error handling
 */
char *STRNDUP(const char *s, size_t n);

/**
 * \brief initialise the configuration data structure
 */
void Config_init(void);

#endif
