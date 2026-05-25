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

#ifdef DEBUG
static MemNode *mem_head = NULL;
static pthread_mutex_t mem_mutex = PTHREAD_MUTEX_INITIALIZER;
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
    log_printf(debug, file, func, line, "MALLOC: %p, %zu bytes\n", ptr, size);

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
        node->next = mem_head;
        mem_head = node;
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
    log_printf(debug, file, func, line, "CALLOC: %p, %zu bytes\n", ptr,
               nmemb * size);

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
        node->next = mem_head;
        mem_head = node;
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

#ifdef DEBUG
    log_printf(debug, file, func, line, "REALLOC: %p, %zu bytes\n", ptr, size);

    // Look up in tracker
    pthread_mutex_lock(&mem_mutex);
    MemNode **curr = &mem_head;
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
        void *new_ptr = realloc(ptr, size);
        if (!new_ptr && size != 0) {
            // Re-link the node before releasing lock and failing
            found_node->next = mem_head;
            mem_head = found_node;
            pthread_mutex_unlock(&mem_mutex);

            fatal_log_printf(file, func, line, "realloc failed: %s!\n",
                             strerror(errno));
            return NULL;
        }

        if (!new_ptr && size == 0) {
            pthread_mutex_unlock(&mem_mutex);
            free(found_node);
            log_printf(debug, file, func, line,
                       "REALLOC result: NULL (size 0, freed)\n");
            return NULL;
        } else {
            found_node->ptr = new_ptr;
            found_node->size = size;
            found_node->file = file;
            found_node->func = func;
            found_node->line = line;

            found_node->next = mem_head;
            mem_head = found_node;
            pthread_mutex_unlock(&mem_mutex);

            log_printf(debug, file, func, line, "REALLOC result: %p\n",
                       new_ptr);
            return new_ptr;
        }
    }
    pthread_mutex_unlock(&mem_mutex);

    // Fallback path: pointer was not found in tracker
    log_printf(warning, file, func, line, "REALLOC: %p not found in tracker!\n",
               ptr);

    void *new_ptr = realloc(ptr, size);
    if (!new_ptr && size != 0) {
        fatal_log_printf(file, func, line, "realloc failed: %s!\n",
                         strerror(errno));
    }

    if (new_ptr || size == 0) {
        if (!new_ptr && size == 0) {
            log_printf(debug, file, func, line,
                       "REALLOC result: %p (size 0, not found, unchanged)\n",
                       new_ptr);
            return new_ptr;
        } else {
            MemNode *node = calloc(1, sizeof(MemNode));
            if (!node) {
                fatal_log_printf(file, func, line,
                                 "Could not allocate MemNode: %s!\n",
                                 strerror(errno));
            }
            node->ptr = new_ptr;
            node->size = size;
            node->file = file;
            node->func = func;
            node->line = line;

            pthread_mutex_lock(&mem_mutex);
            node->next = mem_head;
            mem_head = node;
            pthread_mutex_unlock(&mem_mutex);

            log_printf(debug, file, func, line,
                       "REALLOC result: %p (adopted)\n", new_ptr);
            return new_ptr;
        }
    }

    return NULL;
#else
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr && size != 0) {
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
        log_printf(debug, file, func, line, "REALPATH: %p\n", (void *)res);

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
        node->next = mem_head;
        mem_head = node;
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
    MemNode **curr = &mem_head;
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
        log_printf(debug, file, func, line, "FREE: %p\n", ptr);
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
#ifdef DEBUG
    pthread_mutex_lock(&mem_mutex);
    MemNode *curr = mem_head;
    while (curr) {
        MemNode *next = curr->next;
        free(curr->ptr);
        free(curr);
        curr = next;
    }
    mem_head = NULL;
    pthread_mutex_unlock(&mem_mutex);
    pthread_mutex_destroy(&mem_mutex);
#endif
    if (CONFIG.http_headers) {
        curl_slist_free_all(CONFIG.http_headers);
        CONFIG.http_headers = NULL;
    }
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
