#include "util.h"

#include "config.h"
#include "log.h"

#include <openssl/evp.h>
#include <uuid/uuid.h>

#include <errno.h>
#include <execinfo.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


/**
 * \brief Backtrace buffer size
 */
#define BT_BUF_SIZE 100

/**
 * \brief The length of a MD5SUM string
 */
#define MD5_HASH_LEN 32

/**
 * \brief The length of the salt
 * \details This is basically the length of a UUID
 */
#define SALT_LEN 36

char *path_append(const char *path, const char *filename)
{
    int needs_separator = 0;
    if ((path[strnlen(path, MAX_PATH_LEN) - 1] != '/')
            && (filename[0] != '/')) {
        needs_separator = 1;
    }

    char *str;
    size_t ul = strnlen(path, MAX_PATH_LEN);
    size_t sl = strnlen(filename, MAX_FILENAME_LEN);
    str = CALLOC(ul + sl + needs_separator + 1, sizeof(char));
    strncpy(str, path, ul);
    if (needs_separator) {
        str[ul] = '/';
    }
    strncat(str, filename, sl);
    return str;
}

int64_t round_div(int64_t a, int64_t b)
{
    return (a + (b / 2)) / b;
}

void _PTHREAD_MUTEX_INIT_(pthread_mutex_t *x, const pthread_mutexattr_t *attr,
                          const char *file, const char *func, int line, const char *x_name)
{
    log_printf(debug, file, func, line,
               "%x pthread_mutex_init: %p, %p, %s\n", pthread_self(), x, attr, x_name);
    pthread_mutexattr_t mutex_attr;
    if (attr == NULL) {
        pthread_mutexattr_init(&mutex_attr);
        pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_ERRORCHECK);
        attr = &mutex_attr;
    }
    int ret = pthread_mutex_init(x, attr);
    if (ret) {
        log_printf(fatal, file, func, line,
                   "%x pthread_mutex_init: %d, %s\n", pthread_self(), ret, strerror(ret));
    }
    if (attr == &mutex_attr) {
        pthread_mutexattr_destroy(&mutex_attr);
    }
}

void _PTHREAD_MUTEX_DESTROY_(pthread_mutex_t *x, const char *file,
                             const char *func, int line, const char *x_name)
{
    log_printf(debug, file, func, line,
               "%x pthread_mutex_destroy: %p, %s\n", pthread_self(), x, x_name);
    int ret;
    ret = pthread_mutex_destroy(x);
    if (ret) {
        log_printf(fatal, file, func, line,
                   "%x pthread_mutex_destroy: %d, %s\n", pthread_self(), ret, strerror(ret));
    }
}

void _PTHREAD_MUTEX_UNLOCK_(const char *file, const char *func, int line,
                            pthread_mutex_t *x, const char *x_name)
{
    log_printf(debug, file, func, line,
               "%x pthread_mutex_unlock: %p, %s\n", pthread_self(), x, x_name);
    int i;
    i = pthread_mutex_unlock(x);
    if (i) {
        log_printf(fatal, file, func, line,
                   "%x pthread_mutex_unlock: %d, %s\n", pthread_self(), i, strerror(i));
    }
}

void _PTHREAD_MUTEX_LOCK_(const char *file, const char *func, int line,
                          pthread_mutex_t *x, const char *x_name)
{
    log_printf(debug, file, func, line,
               "%x pthread_mutex_lock: %p, %s\n", pthread_self(), x, x_name);
    int i;
    i = pthread_mutex_lock(x);
    if (i) {
        log_printf(fatal, file, func, line,
                   "%x pthread_mutex_unlock: %d, %s\n", pthread_self(), i, strerror(i));
    }
}

void _SEM_INIT_(sem_t *sem, int pshared, unsigned int value, const char *file,
                const char *func, int line, const char *sem_name)
{
    log_printf(debug, file, func, line,
               "%x sem_init: %p, %d, %u, %s\n", pthread_self(), sem, pshared, value, sem_name);
    int i;
    i = sem_init(sem, pshared, value);
    if (i) {
        log_printf(fatal, file, func, line,
                   "%x sem_init: %d, %s\n", pthread_self(), i, strerror(i));
    }
}

void _SEM_DESTROY_(sem_t *sem, const char *file, const char *func, int line,
                   const char *sem_name)
{
    log_printf(debug, file, func, line,
               "%x sem_destroy: %p, %s\n", pthread_self(), sem, sem_name);
    int i;
    i = sem_destroy(sem);
    if (i) {
        log_printf(fatal, file, func, line,
                   "%x sem_destroy: %d, %s\n", pthread_self(), i, strerror(i));
    }
}

void _SEM_WAIT_(const char *file, const char *func, int line, sem_t *sem,
                const char *sem_name)
{
#ifdef DEBUG
    int j;
    if (sem_getvalue(sem, &j)) {
        log_printf(fatal, file, func, line,
                   "%x sem_getvalue: %s\n", pthread_self(), strerror(errno));
    }
    log_printf(debug, file, func, line,
               "%x sem_wait: %p, %s, value: %d\n", pthread_self(), sem, sem_name, j);
#endif
    int i;
    i = sem_wait(sem);
    if (i) {
        log_printf(fatal, file, func, line,
                   "%x sem_wait: %d, %s\n", pthread_self(), i, strerror(i));
    }
}

void _SEM_POST_(const char *file, const char *func, int line, sem_t *sem,
                const char *sem_name)
{
    log_printf(debug, file, func, line,
               "%x sem_post: %p, %s\n", pthread_self(), sem, sem_name);
    int i;
    i = sem_post(sem);
    if (i) {
        log_printf(fatal, file, func, line,
                   "%x sem_post: %d, %s\n", pthread_self(), i, strerror(i));
    }
}

void exit_failure(void)
{
    int nptrs;
    void *buffer[BT_BUF_SIZE];

    nptrs = backtrace(buffer, BT_BUF_SIZE);
    fprintf(stderr, "\nOops! HTTPDirFS crashed! :(\n");
    fprintf(stderr, "backtrace() returned the following %d addresses:\n",
            nptrs);
    backtrace_symbols_fd(buffer, nptrs, STDERR_FILENO);

    exit(EXIT_FAILURE);
}

void erase_string(FILE *file, size_t max_len, char *s)
{
    size_t l = strnlen(s, max_len);
    for (size_t k = 0; k < l; k++) {
        fprintf(file, "\b");
    }
    for (size_t k = 0; k < l; k++) {
        fprintf(file, " ");
    }
    for (size_t k = 0; k < l; k++) {
        fprintf(file, "\b");
    }
}

char *generate_salt(void)
{
    char *out;
    out = CALLOC(SALT_LEN + 1, sizeof(char));
    uuid_t uu;
    uuid_generate(uu);
    uuid_unparse(uu, out);
    return out;
}

char *generate_md5sum(const char *str)
{
    size_t len = strnlen(str, MAX_PATH_LEN);
    char *out = CALLOC(MD5_HASH_LEN + 1, sizeof(char));

    EVP_MD_CTX *mdctx;
    unsigned char *md5_digest;
    unsigned int md5_digest_len = EVP_MD_size(EVP_md5());

    // MD5_Init
    mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, EVP_md5(), NULL);

    // MD5_Update
    EVP_DigestUpdate(mdctx, str, len);

    // MD5_Final
    md5_digest = (unsigned char *)OPENSSL_malloc(md5_digest_len);
    EVP_DigestFinal_ex(mdctx, md5_digest, &md5_digest_len);
    EVP_MD_CTX_free(mdctx);

    for (unsigned int i = 0; i < md5_digest_len; i++) {
        sprintf(out + 2 * i, "%02x", md5_digest[i]);
    }
    return out;
}

void *CALLOC(size_t nmemb, size_t size)
{
    void *ptr = calloc(nmemb, size);
    if (!ptr) {
        lprintf(fatal, "%s!\n", strerror(errno));
    }
    return ptr;
}

void FREE(void *ptr)
{
    if (ptr) {
        free(ptr);
    } else {
        lprintf(fatal, "attempted to free NULL ptr!\n");
    }
}

char *str_to_hex(char *s)
{
    char *hex = CALLOC(strnlen(s, MAX_PATH_LEN) * 2 + 1, sizeof(char));
    for (char *c = s, *h = hex; *c; c++, h += 2) {
        sprintf(h, "%x", *c);
    }
    return hex;
}
