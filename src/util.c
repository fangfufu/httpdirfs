#include "util.h"

#include "config.h"
#include "log.h"

#include <curl/curl.h>
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

static MemNode *mem_head = NULL;
static pthread_mutex_t mem_mutex = PTHREAD_MUTEX_INITIALIZER;

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

void pthread_mutex_init_wrapper(pthread_mutex_t *x,
                                const pthread_mutexattr_t *attr,
                                const char *file, const char *func, int line,
                                const char *x_name)
{
    log_printf(debug, file, func, line, "%lx pthread_mutex_init: %p, %p, %s\n",
               (unsigned long)pthread_self(), (void *)x, (const void *)attr,
               x_name);
    pthread_mutexattr_t mutex_attr;
    if (attr == NULL) {
        pthread_mutexattr_init(&mutex_attr);
        pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_ERRORCHECK);
        attr = &mutex_attr;
    }
    int ret = pthread_mutex_init(x, attr);
    if (ret) {
        fatal_log_printf(file, func, line, "%lx pthread_mutex_init: %d, %s\n",
                         (unsigned long)pthread_self(), ret, strerror(ret));
    }
    if (attr == &mutex_attr) {
        pthread_mutexattr_destroy(&mutex_attr);
    }
}

void pthread_mutex_destroy_wrapper(pthread_mutex_t *x, const char *file,
                                   const char *func, int line,
                                   const char *x_name)
{
    log_printf(debug, file, func, line, "%lx pthread_mutex_destroy: %p, %s\n",
               (unsigned long)pthread_self(), (void *)x, x_name);
    int ret;
    ret = pthread_mutex_destroy(x);
    if (ret) {
        fatal_log_printf(file, func, line,
                         "%lx pthread_mutex_destroy: %d, %s\n",
                         (unsigned long)pthread_self(), ret, strerror(ret));
    }
}

void pthread_mutex_unlock_wrapper(const char *file, const char *func, int line,
                                  pthread_mutex_t *x, const char *x_name)
{
    log_printf(debug, file, func, line, "%lx pthread_mutex_unlock: %p, %s\n",
               (unsigned long)pthread_self(), (void *)x, x_name);
    int i;
    i = pthread_mutex_unlock(x);
    if (i) {
        fatal_log_printf(file, func, line, "%lx pthread_mutex_unlock: %d, %s\n",
                         (unsigned long)pthread_self(), i, strerror(i));
    }
}

void pthread_mutex_lock_wrapper(const char *file, const char *func, int line,
                                pthread_mutex_t *x, const char *x_name)
{
    log_printf(debug, file, func, line, "%lx pthread_mutex_lock: %p, %s\n",
               (unsigned long)pthread_self(), (void *)x, x_name);
    int i;
    i = pthread_mutex_lock(x);
    if (i) {
        fatal_log_printf(file, func, line, "%lx pthread_mutex_lock: %d, %s\n",
                         (unsigned long)pthread_self(), i, strerror(i));
    }
}

void sem_init_wrapper(sem_t *sem, int pshared, unsigned int value,
                      const char *file, const char *func, int line,
                      const char *sem_name)
{
    log_printf(debug, file, func, line, "%lx sem_init: %p, %d, %u, %s\n",
               (unsigned long)pthread_self(), (void *)sem, pshared, value,
               sem_name);
    int i;
    i = sem_init(sem, pshared, value);
    if (i) {
        int saved_errno = errno;
        fatal_log_printf(file, func, line, "%lx sem_init: %d, %s\n",
                         (unsigned long)pthread_self(), saved_errno,
                         strerror(saved_errno));
    }
}

void sem_destroy_wrapper(sem_t *sem, const char *file, const char *func,
                         int line, const char *sem_name)
{
    log_printf(debug, file, func, line, "%lx sem_destroy: %p, %s\n",
               (unsigned long)pthread_self(), (void *)sem, sem_name);
    int i;
    i = sem_destroy(sem);
    if (i) {
        int saved_errno = errno;
        fatal_log_printf(file, func, line, "%lx sem_destroy: %d, %s\n",
                         (unsigned long)pthread_self(), saved_errno,
                         strerror(saved_errno));
    }
}

void sem_wait_wrapper(const char *file, const char *func, int line, sem_t *sem,
                      const char *sem_name)
{
    int j;
    if (sem_getvalue(sem, &j)) {
        int saved_errno = errno;
        fatal_log_printf(file, func, line, "%lx sem_getvalue: %s\n",
                         (unsigned long)pthread_self(), strerror(saved_errno));
    }
    log_printf(debug, file, func, line, "%lx sem_wait: %p, %s, value: %d\n",
               (unsigned long)pthread_self(), (void *)sem, sem_name, j);
    int i;
    i = sem_wait(sem);
    if (i) {
        int saved_errno = errno;
        fatal_log_printf(file, func, line, "%lx sem_wait: %d, %s\n",
                         (unsigned long)pthread_self(), saved_errno,
                         strerror(saved_errno));
    }
}

void sem_post_wrapper(const char *file, const char *func, int line, sem_t *sem,
                      const char *sem_name)
{
    log_printf(debug, file, func, line, "%lx sem_post: %p, %s\n",
               (unsigned long)pthread_self(), (void *)sem, sem_name);
    int i;
    i = sem_post(sem);
    if (i) {
        int saved_errno = errno;
        fatal_log_printf(file, func, line, "%lx sem_post: %d, %s\n",
                         (unsigned long)pthread_self(), saved_errno,
                         strerror(saved_errno));
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
        sprintf(out + (2 * (size_t)i), "%02x", md5_digest[i]);
    }
    return out;
}

void *CALLOC_wrapper(size_t nmemb, size_t size, const char *file,
                     const char *func, int line)
{
    void *ptr = calloc(nmemb, size);
    if (!ptr) {
        fatal_log_printf(file, func, line, "%s!\n", strerror(errno));
    }

    log_printf(debug, file, func, line, "CALLOC: %p, %zu bytes\n", ptr,
               nmemb * size);

    MemNode *node = calloc(1, sizeof(MemNode));
    if (!node) {
        fatal_log_printf(file, func, line, "Could not allocate MemNode: %s!\n",
                         strerror(errno));
    }
    node->ptr = ptr;
    node->size = nmemb * size;
    node->file = file;
    node->func = func;
    node->line = line;

    pthread_mutex_lock(&mem_mutex);
    node->next = mem_head;
    mem_head = node;
    pthread_mutex_unlock(&mem_mutex);

    return ptr;
}

char *STRDUP_wrapper(const char *s, const char *file, const char *func,
                     int line)
{
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s) + 1;
    char *ptr = CALLOC_wrapper(len, sizeof(char), file, func, line);
    if (ptr) {
        memcpy(ptr, s, len);
    }
    return ptr;
}

char *STRNDUP_wrapper(const char *s, size_t n, const char *file,
                      const char *func, int line)
{
    if (!s) {
        return NULL;
    }
    size_t len = strnlen(s, n);
    char *ptr = CALLOC_wrapper(len + 1, sizeof(char), file, func, line);
    if (ptr) {
        memcpy(ptr, s, len);
        ptr[len] = '\0';
    }
    return ptr;
}

void *REALLOC_wrapper(void *ptr, size_t size, const char *file,
                      const char *func, int line)
{
    if (!ptr) {
        return CALLOC_wrapper(1, size, file, func, line);
    }

    pthread_mutex_lock(&mem_mutex);
    MemNode **curr = &mem_head;
    while (*curr) {
        if ((*curr)->ptr == ptr) {
            MemNode *node = *curr;
            log_printf(debug, file, func, line, "REALLOC: %p, %zu bytes\n", ptr,
                       size);
            void *new_ptr = realloc(ptr, size);
            if (!new_ptr) {
                pthread_mutex_unlock(&mem_mutex);
                fatal_log_printf(file, func, line, "realloc failed: %s!\n",
                                 strerror(errno));
            }
            node->ptr = new_ptr;
            node->size = size;
            node->file = file;
            node->func = func;
            node->line = line;
            pthread_mutex_unlock(&mem_mutex);
            log_printf(debug, file, func, line, "REALLOC result: %p\n",
                       new_ptr);
            return new_ptr;
        }
        curr = &((*curr)->next);
    }
    pthread_mutex_unlock(&mem_mutex);

    log_printf(warning, file, func, line, "REALLOC: %p not found in tracker!\n",
               ptr);
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        fatal_log_printf(file, func, line, "realloc failed: %s!\n",
                         strerror(errno));
    }
    return new_ptr;
}

char *REALPATH_wrapper(const char *path, char *resolved_path, const char *file,
                       const char *func, int line)
{
    char *res = realpath(path, resolved_path);
    if (res && !resolved_path) {
        log_printf(debug, file, func, line, "REALPATH: %p\n", (void *)res);
        // We need to track the pointer returned by realpath
        // But realpath uses malloc internally, so we should untrack it with
        // free() later. Wait, our FREE_wrapper already handles pointers not in
        // the tracker by calling free(). But if we want it to be deallocated at
        // exit, we SHOULD track it.

        MemNode *node = calloc(1, sizeof(MemNode));
        if (!node) {
            fatal_log_printf(file, func, line,
                             "Could not allocate MemNode: %s!\n",
                             strerror(errno));
        }
        node->ptr = res;
        node->size = strlen(res) + 1; // Store actual length of allocated string
        node->file = file;
        node->func = func;
        node->line = line;

        pthread_mutex_lock(&mem_mutex);
        node->next = mem_head;
        mem_head = node;
        pthread_mutex_unlock(&mem_mutex);
    }
    return res;
}

void FREE_wrapper(void *ptr, const char *file, const char *func, int line)
{
    if (!ptr) {
        return;
    }

    pthread_mutex_lock(&mem_mutex);
    MemNode **curr = &mem_head;
    while (*curr) {
        if ((*curr)->ptr == ptr) {
            MemNode *to_free = *curr;
            *curr = to_free->next;
            free(to_free);
            goto found;
        }
        curr = &((*curr)->next);
    }
    pthread_mutex_unlock(&mem_mutex);

    log_printf(warning, file, func, line, "FREE: %p not found in tracker!\n",
               ptr);
    free(ptr);
    return;

found:
    pthread_mutex_unlock(&mem_mutex);
    log_printf(debug, file, func, line, "FREE: %p\n", ptr);
    free(ptr);
}

void mem_cleanup(void)
{
    if (pthread_mutex_trylock(&mem_mutex) != 0) {
        goto cleanup_headers;
    }
    MemNode *curr = mem_head;
    while (curr) {
        MemNode *next = curr->next;
        free(curr->ptr);
        free(curr);
        curr = next;
    }
    mem_head = NULL;
    pthread_mutex_unlock(&mem_mutex);

cleanup_headers:
    if (CONFIG.http_headers) {
        curl_slist_free_all(CONFIG.http_headers);
        CONFIG.http_headers = NULL;
    }
}

char *str_to_hex(char *s)
{
    char *hex = CALLOC((strnlen(s, MAX_PATH_LEN) * 2) + 1, sizeof(char));
    for (char *c = s, *h = hex; *c; c++, h += 2) {
        sprintf(h, "%x", *c);
    }
    return hex;
}
