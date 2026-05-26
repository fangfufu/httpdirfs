#include "util.h"

#include "config.h"
#include "log.h"

#ifdef DEBUG
#include "link.h"
#include "cache.h"
#endif

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

#ifdef DEBUG
#include <stdint.h>
#define MEM_HASH_SIZE 8192
static MemNode *mem_hash_table[MEM_HASH_SIZE] = {NULL};
static pthread_mutex_t mem_mutex = PTHREAD_MUTEX_INITIALIZER;

static inline size_t hash_ptr(const void *ptr)
{
    uintptr_t v = (uintptr_t)ptr;
    return ((v >> 3) ^ (v >> 16)) & (MEM_HASH_SIZE - 1);
}
#endif

char *path_append(const char *path, const char *filename)
{
    if (!path || !filename) {
        lprintf(fatal, "path_append: path or filename is NULL\n");
    }
    size_t ul = strnlen(path, PATH_MAX);
    size_t fl = strnlen(filename, PATH_MAX);
    size_t skip = 0;
    int needs_separator = 0;
    const char *f = filename;

    if (ul > 0) {
        if (path[ul - 1] != '/') {
            needs_separator = 1;
        }
        while (skip < fl && filename[skip] == '/') {
            skip++;
        }
        f = filename + skip;
    }

    char *str;
    size_t sl = fl - skip;
    str = CALLOC(ul + sl + needs_separator + 1, sizeof(char));
    memcpy(str, path, ul);
    if (needs_separator) {
        str[ul] = '/';
    }
    memcpy(str + ul + needs_separator, f, sl);
    return str;
}

void pthread_mutex_init_wrapper(pthread_mutex_t *x,
                                const pthread_mutexattr_t *attr,
                                const char *file, const char *func, int line)
{
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
                                   const char *func, int line)
{
    int ret;
    ret = pthread_mutex_destroy(x);
    if (ret) {
        fatal_log_printf(file, func, line,
                         "%lx pthread_mutex_destroy: %d, %s\n",
                         (unsigned long)pthread_self(), ret, strerror(ret));
    }
}

void pthread_mutex_unlock_wrapper(const char *file, const char *func, int line,
                                  pthread_mutex_t *x)
{
    int i;
    i = pthread_mutex_unlock(x);
    if (i) {
        fatal_log_printf(file, func, line, "%lx pthread_mutex_unlock: %d, %s\n",
                         (unsigned long)pthread_self(), i, strerror(i));
    }
}

void pthread_mutex_lock_wrapper(const char *file, const char *func, int line,
                                pthread_mutex_t *x)
{
    int i;
    i = pthread_mutex_lock(x);
    if (i) {
        fatal_log_printf(file, func, line, "%lx pthread_mutex_lock: %d, %s\n",
                         (unsigned long)pthread_self(), i, strerror(i));
    }
}

#ifdef __APPLE__

void sem_init_wrapper(sys_sem_t *sem, int pshared, unsigned int value,
                      const char *file, const char *func, int line)
{
    (void)pshared;
    int ret = pthread_mutex_init(&sem->mutex, NULL);
    if (ret) {
        fatal_log_printf(
            file, func, line,
            "%lx pthread_mutex_init for semaphore failed: %d, %s\n",
            (unsigned long)pthread_self(), ret, strerror(ret));
    }
    ret = pthread_cond_init(&sem->cond, NULL);
    if (ret) {
        pthread_mutex_destroy(&sem->mutex);
        fatal_log_printf(file, func, line,
                         "%lx pthread_cond_init for semaphore failed: %d, %s\n",
                         (unsigned long)pthread_self(), ret, strerror(ret));
    }
    sem->count = (int)value;
}

void sem_destroy_wrapper(sys_sem_t *sem, const char *file, const char *func,
                         int line)
{
    int ret = pthread_mutex_destroy(&sem->mutex);
    if (ret) {
        fatal_log_printf(
            file, func, line,
            "%lx pthread_mutex_destroy for semaphore failed: %d, %s\n",
            (unsigned long)pthread_self(), ret, strerror(ret));
    }
    ret = pthread_cond_destroy(&sem->cond);
    if (ret) {
        fatal_log_printf(
            file, func, line,
            "%lx pthread_cond_destroy for semaphore failed: %d, %s\n",
            (unsigned long)pthread_self(), ret, strerror(ret));
    }
}

void sem_wait_wrapper(const char *file, const char *func, int line,
                      sys_sem_t *sem)
{
    int ret = pthread_mutex_lock(&sem->mutex);
    if (ret) {
        fatal_log_printf(
            file, func, line,
            "%lx pthread_mutex_lock for semaphore failed: %d, %s\n",
            (unsigned long)pthread_self(), ret, strerror(ret));
    }
    while (sem->count <= 0) {
        ret = pthread_cond_wait(&sem->cond, &sem->mutex);
        if (ret) {
            pthread_mutex_unlock(&sem->mutex);
            fatal_log_printf(
                file, func, line,
                "%lx pthread_cond_wait for semaphore failed: %d, %s\n",
                (unsigned long)pthread_self(), ret, strerror(ret));
        }
    }
    sem->count--;
    ret = pthread_mutex_unlock(&sem->mutex);
    if (ret) {
        fatal_log_printf(
            file, func, line,
            "%lx pthread_mutex_unlock for semaphore failed: %d, %s\n",
            (unsigned long)pthread_self(), ret, strerror(ret));
    }
}

void sem_post_wrapper(const char *file, const char *func, int line,
                      sys_sem_t *sem)
{
    int ret = pthread_mutex_lock(&sem->mutex);
    if (ret) {
        fatal_log_printf(
            file, func, line,
            "%lx pthread_mutex_lock for semaphore failed: %d, %s\n",
            (unsigned long)pthread_self(), ret, strerror(ret));
    }
    sem->count++;
    ret = pthread_cond_signal(&sem->cond);
    if (ret) {
        pthread_mutex_unlock(&sem->mutex);
        fatal_log_printf(
            file, func, line,
            "%lx pthread_cond_signal for semaphore failed: %d, %s\n",
            (unsigned long)pthread_self(), ret, strerror(ret));
    }
    ret = pthread_mutex_unlock(&sem->mutex);
    if (ret) {
        fatal_log_printf(
            file, func, line,
            "%lx pthread_mutex_unlock for semaphore failed: %d, %s\n",
            (unsigned long)pthread_self(), ret, strerror(ret));
    }
}

int sys_sem_trywait_wrapper(const char *file, const char *func, int line,
                            sys_sem_t *sem)
{
    int ret = 0;
    int err = pthread_mutex_lock(&sem->mutex);
    if (err) {
        fatal_log_printf(
            file, func, line,
            "%lx pthread_mutex_lock for semaphore failed: %d, %s\n",
            (unsigned long)pthread_self(), err, strerror(err));
    }
    if (sem->count > 0) {
        sem->count--;
    } else {
        ret = -1;
    }
    err = pthread_mutex_unlock(&sem->mutex);
    if (err) {
        fatal_log_printf(
            file, func, line,
            "%lx pthread_mutex_unlock for semaphore failed: %d, %s\n",
            (unsigned long)pthread_self(), err, strerror(err));
    }
    if (ret == -1) {
        errno = EAGAIN;
    }
    return ret;
}

#else

void sem_init_wrapper(sys_sem_t *sem, int pshared, unsigned int value,
                      const char *file, const char *func, int line)
{
    int i;
    i = sem_init(sem, pshared, value);
    if (i) {
        int saved_errno = errno;
        fatal_log_printf(file, func, line, "%lx sem_init: %d, %s\n",
                         (unsigned long)pthread_self(), saved_errno,
                         strerror(saved_errno));
    }
}

void sem_destroy_wrapper(sys_sem_t *sem, const char *file, const char *func,
                         int line)
{
    int i;
    i = sem_destroy(sem);
    if (i) {
        int saved_errno = errno;
        fatal_log_printf(file, func, line, "%lx sem_destroy: %d, %s\n",
                         (unsigned long)pthread_self(), saved_errno,
                         strerror(saved_errno));
    }
}

void sem_wait_wrapper(const char *file, const char *func, int line,
                      sys_sem_t *sem)
{
    int i;
    i = sem_wait(sem);
    if (i) {
        int saved_errno = errno;
        fatal_log_printf(file, func, line, "%lx sem_wait: %d, %s\n",
                         (unsigned long)pthread_self(), saved_errno,
                         strerror(saved_errno));
    }
}

void sem_post_wrapper(const char *file, const char *func, int line,
                      sys_sem_t *sem)
{
    int i;
    i = sem_post(sem);
    if (i) {
        int saved_errno = errno;
        fatal_log_printf(file, func, line, "%lx sem_post: %d, %s\n",
                         (unsigned long)pthread_self(), saved_errno,
                         strerror(saved_errno));
    }
}

int sys_sem_trywait_wrapper(const char *file, const char *func, int line,
                            sys_sem_t *sem)
{
    int i = sem_trywait(sem);
    if (i) {
        int saved_errno = errno;
        if (saved_errno != EAGAIN) {
            fatal_log_printf(file, func, line, "%lx sem_trywait: %d, %s\n",
                             (unsigned long)pthread_self(), saved_errno,
                             strerror(saved_errno));
        }
    }
    return i;
}

#endif

void pthread_cond_init_wrapper(pthread_cond_t *cond,
                               const pthread_condattr_t *attr, const char *file,
                               const char *func, int line)
{
    int ret = pthread_cond_init(cond, attr);
    if (ret) {
        fatal_log_printf(file, func, line, "%lx pthread_cond_init: %d, %s\n",
                         (unsigned long)pthread_self(), ret, strerror(ret));
    }
}

void pthread_cond_destroy_wrapper(pthread_cond_t *cond, const char *file,
                                  const char *func, int line)
{
    int ret = pthread_cond_destroy(cond);
    if (ret) {
        fatal_log_printf(file, func, line, "%lx pthread_cond_destroy: %d, %s\n",
                         (unsigned long)pthread_self(), ret, strerror(ret));
    }
}

void pthread_cond_broadcast_wrapper(const char *file, const char *func,
                                    int line, pthread_cond_t *cond)
{
    int ret = pthread_cond_broadcast(cond);
    if (ret) {
        fatal_log_printf(file, func, line,
                         "%lx pthread_cond_broadcast: %d, %s\n",
                         (unsigned long)pthread_self(), ret, strerror(ret));
    }
}

void pthread_cond_wait_wrapper(const char *file, const char *func, int line,
                               pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    int ret = pthread_cond_wait(cond, mutex);
    if (ret) {
        fatal_log_printf(file, func, line, "%lx pthread_cond_wait: %d, %s\n",
                         (unsigned long)pthread_self(), ret, strerror(ret));
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
    size_t len = strnlen(str, PATH_MAX);
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

    OPENSSL_free(md5_digest);
    return out;
}

static void *malloc_wrapper_internal(size_t size, const char *file,
                                     const char *func, int line)
{
    void *ptr = malloc(size);
    if (size != 0 && !ptr) {
        fatal_log_printf(file, func, line, "%s!\n", strerror(errno));
    }

#ifdef DEBUG
    if (ptr) {
        MemNode *node = calloc(1, sizeof(MemNode));
        if (!node) {
            fatal_log_printf(file, func, line,
                             "Could not allocate MemNode: %s!\n",
                             strerror(errno));
        }
        node->ptr = ptr;
        node->size = size;
        node->file = file;
        node->func = func;
        node->line = line;

        pthread_mutex_lock(&mem_mutex);
        size_t idx = hash_ptr(ptr);
        node->next = mem_hash_table[idx];
        mem_hash_table[idx] = node;
        pthread_mutex_unlock(&mem_mutex);
    }
#endif

    return ptr;
}

void *CALLOC_wrapper(size_t nmemb, size_t size, const char *file,
                     const char *func, int line)
{
    void *ptr = calloc(nmemb, size);
    if (nmemb != 0 && size != 0 && !ptr) {
        fatal_log_printf(file, func, line, "%s!\n", strerror(errno));
    }

#ifdef DEBUG
    if (ptr) {
        MemNode *node = calloc(1, sizeof(MemNode));
        if (!node) {
            fatal_log_printf(file, func, line,
                             "Could not allocate MemNode: %s!\n",
                             strerror(errno));
        }
        node->ptr = ptr;
        node->size = nmemb * size;
        node->file = file;
        node->func = func;
        node->line = line;

        pthread_mutex_lock(&mem_mutex);
        size_t idx = hash_ptr(ptr);
        node->next = mem_hash_table[idx];
        mem_hash_table[idx] = node;
        pthread_mutex_unlock(&mem_mutex);
    }
#endif

    return ptr;
}

char *STRDUP_wrapper(const char *s, const char *file, const char *func,
                     int line)
{
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s) + 1;
    char *ptr = malloc_wrapper_internal(len, file, func, line);
    memcpy(ptr, s, len);
    return ptr;
}

char *STRNDUP_wrapper(const char *s, size_t n, const char *file,
                      const char *func, int line)
{
    if (!s) {
        return NULL;
    }
    size_t len = strnlen(s, n);
    char *ptr = malloc_wrapper_internal(len + 1, file, func, line);
    memcpy(ptr, s, len);
    ptr[len] = '\0';
    return ptr;
}

void *REALLOC_wrapper(void *ptr, size_t size, const char *file,
                      const char *func, int line)
{
    if (!ptr) {
        return malloc_wrapper_internal(size, file, func, line);
    }

    if (size == 0) {
        FREE_wrapper(ptr, file, func, line);
        return NULL;
    }

#ifdef DEBUG
    // Look up in tracker
    pthread_mutex_lock(&mem_mutex);
    size_t idx = hash_ptr(ptr);
    MemNode **curr = &mem_hash_table[idx];
    MemNode *found_node = NULL;
    while (*curr) {
        if ((*curr)->ptr == ptr) {
            found_node = *curr;
            *curr = found_node->next; // Temporarily unlink
            break;
        }
        curr = &((*curr)->next);
    }

    if (found_node) {
        pthread_mutex_unlock(&mem_mutex);
        void *new_ptr = realloc(ptr, size);
        if (!new_ptr) {
            pthread_mutex_lock(&mem_mutex);
            found_node->next = mem_hash_table[idx];
            mem_hash_table[idx] = found_node;
            pthread_mutex_unlock(&mem_mutex);

            fatal_log_printf(file, func, line, "realloc failed: %s!\n",
                             strerror(errno));
            return NULL;
        }

        found_node->ptr = new_ptr;
        found_node->size = size;
        found_node->file = file;
        found_node->func = func;
        found_node->line = line;

        pthread_mutex_lock(&mem_mutex);
        size_t new_idx = hash_ptr(new_ptr);
        found_node->next = mem_hash_table[new_idx];
        mem_hash_table[new_idx] = found_node;
        pthread_mutex_unlock(&mem_mutex);

        return new_ptr;
    }
    pthread_mutex_unlock(&mem_mutex);

    // Fallback path: pointer was not found in tracker
    log_printf(warning, file, func, line, "REALLOC: %p not found in tracker!\n",
               ptr);

    void *new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        fatal_log_printf(file, func, line, "realloc failed: %s!\n",
                         strerror(errno));
        return NULL;
    }

    MemNode *node = calloc(1, sizeof(MemNode));
    if (!node) {
        fatal_log_printf(file, func, line, "Could not allocate MemNode: %s!\n",
                         strerror(errno));
    }
    node->ptr = new_ptr;
    node->size = size;
    node->file = file;
    node->func = func;
    node->line = line;

    pthread_mutex_lock(&mem_mutex);
    size_t new_idx = hash_ptr(new_ptr);
    node->next = mem_hash_table[new_idx];
    mem_hash_table[new_idx] = node;
    pthread_mutex_unlock(&mem_mutex);

    return new_ptr;
#else
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        fatal_log_printf(file, func, line, "realloc failed: %s!\n",
                         strerror(errno));
    }
    return new_ptr;
#endif
}

char *REALPATH_wrapper(const char *path, char *resolved_path, const char *file,
                       const char *func, int line)
{
    char *res = realpath(path, resolved_path);
#ifdef DEBUG
    if (res && !resolved_path) {
        MemNode *node = calloc(1, sizeof(MemNode));
        if (!node) {
            fatal_log_printf(file, func, line,
                             "Could not allocate MemNode: %s!\n",
                             strerror(errno));
        }
        node->ptr = res;
        node->size = strlen(res) + 1;
        node->file = file;
        node->func = func;
        node->line = line;

        pthread_mutex_lock(&mem_mutex);
        size_t idx = hash_ptr(res);
        node->next = mem_hash_table[idx];
        mem_hash_table[idx] = node;
        pthread_mutex_unlock(&mem_mutex);
    }
#else
    (void)file;
    (void)func;
    (void)line;
#endif
    return res;
}

void FREE_wrapper(void *ptr, const char *file, const char *func, int line)
{
    if (!ptr) {
        return;
    }

#ifdef DEBUG
    pthread_mutex_lock(&mem_mutex);
    size_t idx = hash_ptr(ptr);
    MemNode **curr = &mem_hash_table[idx];
    MemNode *found_node = NULL;
    while (*curr) {
        if ((*curr)->ptr == ptr) {
            found_node = *curr;
            *curr = found_node->next;
            break;
        }
        curr = &((*curr)->next);
    }
    pthread_mutex_unlock(&mem_mutex);

    if (found_node) {
        free(found_node);
        free(ptr);
        return;
    }

    log_printf(warning, file, func, line, "FREE: %p not found in tracker!\n",
               ptr);
#else
    (void)file;
    (void)func;
    (void)line;
#endif
    free(ptr);
}

void mem_cleanup(void)
{
    if (CONFIG.http_headers) {
        curl_slist_free_all(CONFIG.http_headers);
        CONFIG.http_headers = NULL;
    }

#ifdef DEBUG
    // 1. Traverse and tear down the whole filesystem recursively
    if (ROOT_LINK_TBL) {
        LinkTable_free(ROOT_LINK_TBL);
        ROOT_LINK_TBL = NULL;
    }

    // 2. Clean up any other heap-allocated cache directories
    CacheSystem_cleanup();

    // 3. Clean up CONFIG heap strings
    Config_cleanup();

    // 4. Consult the memory allocation tracker to check for leaks
    pthread_mutex_lock(&mem_mutex);
    int leak_count = 0;
    for (size_t i = 0; i < MEM_HASH_SIZE; i++) {
        MemNode *curr = mem_hash_table[i];
        while (curr) {
            leak_count++;
            curr = curr->next;
        }
    }

    if (leak_count > 0) {
        lprintf(error, "======================================================="
                       "===============\n");
        lprintf(error, "                     MEMORY LEAK REPORT\n");
        lprintf(error, "======================================================="
                       "===============\n");
        for (size_t i = 0; i < MEM_HASH_SIZE; i++) {
            MemNode *curr = mem_hash_table[i];
            while (curr) {
                lprintf(error, "[LEAK] Address: %p | Size: %zu bytes\n",
                        curr->ptr, curr->size);
                lprintf(error, "       Allocated at: %s:%d in %s()\n",
                        curr->file, curr->line, curr->func);
                lprintf(error,
                        "---------------------------------------------------"
                        "-------------------\n");
                curr = curr->next;
            }
        }
        lprintf(error, "Total Leaks: %d\n", leak_count);
        lprintf(error, "======================================================="
                       "===============\n");
    } else {
        lprintf(info, "========================================================"
                      "==============\n");
        lprintf(info, "No memory leaks detected!\n");
        lprintf(info, "========================================================"
                      "==============\n");
    }

    // Now forcefully free whatever was leaked so OS exit is completely clean
    for (size_t i = 0; i < MEM_HASH_SIZE; i++) {
        MemNode *curr = mem_hash_table[i];
        while (curr) {
            MemNode *next = curr->next;
            free(curr->ptr);
            free(curr);
            curr = next;
        }
        mem_hash_table[i] = NULL;
    }
    pthread_mutex_unlock(&mem_mutex);
    pthread_mutex_destroy(&mem_mutex);
#endif
}

char *str_to_hex(char *s)
{
    size_t len = strnlen(s, PATH_MAX);
    char *hex = CALLOC((len * 2) + 1, sizeof(char));
    char *h = hex;
    for (size_t i = 0; i < len; i++, h += 2) {
        snprintf(h, 3, "%02x", (unsigned char)s[i]);
    }
    return hex;
}
