#include "util.h"

#include <openssl/md5.h>
#include <uuid/uuid.h>

#include <execinfo.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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

/**
 * \brief The default maximum number of network connections
 */
#define DEFAULT_NETWORK_MAX_CONNS   10

/**
 * \brief The default HTTP 429 (too many requests) wait time
 */
#define DEFAULT_HTTP_WAIT_SEC       5
/**
 * \brief Data file block size
 * \details We set it to 1024*1024*8 = 8MiB
 */
#define DEFAULT_DATA_BLKSZ         8*1024*1024

/**
 * \brief Maximum segment block count
 * \details This is set to 128*1024 blocks, which uses 128KB. By default,
 * this allows the user to store (128*1024)*(8*1024*1024) = 1TB of data
 */
#define DEFAULT_MAX_SEGBC           128*1024

ConfigStruct CONFIG;

/**
 * \note The opening curly bracket should be at line 39, so the code lines up
 * with the definition code in util.h.
 */
void Config_init(void)
{
    /*---------------- Network related --------------*/
    CONFIG.http_username = NULL;

    CONFIG.http_password = NULL;

    CONFIG.proxy = NULL;

    CONFIG.proxy_username = NULL;

    CONFIG.proxy_password = NULL;

    CONFIG.max_conns = DEFAULT_NETWORK_MAX_CONNS;

    CONFIG.user_agent = DEFAULT_USER_AGENT;

    CONFIG.http_wait_sec = DEFAULT_HTTP_WAIT_SEC;

    /*--------------- Cache related ---------------*/
    CONFIG.cache_enabled = 0;

    CONFIG.cache_dir = NULL;

    CONFIG.data_blksz = DEFAULT_DATA_BLKSZ;

    CONFIG.max_segbc = DEFAULT_MAX_SEGBC;

    /*-------------- Sonic related -------------*/
    CONFIG.sonic_mode = 0;

    CONFIG.sonic_username = NULL;

    CONFIG.sonic_password = NULL;

    CONFIG.sonic_id3 = 0;
}

char *path_append(const char *path, const char *filename)
{
    int needs_separator = 0;
    if ((path[strnlen(path, MAX_PATH_LEN)-1] != '/') && (filename[0] != '/')) {
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

void PTHREAD_MUTEX_UNLOCK(pthread_mutex_t *x)
{
    int i;
    i = pthread_mutex_unlock(x);
    if (i) {
        fprintf(stderr, "thread %lu: pthread_mutex_unlock() failed, %d, %s\n",
                pthread_self(), i, strerror(i));
        exit_failure();
    }
}

void PTHREAD_MUTEX_LOCK(pthread_mutex_t *x)
{
    int i;
    i = pthread_mutex_lock(x);
    if (i) {
        fprintf(stderr, "thread %lu: pthread_mutex_lock() failed, %d, %s\n",
                pthread_self(), i, strerror(i));
        exit_failure();
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
    MD5_CTX c;
    unsigned char md5[MD5_DIGEST_LENGTH];
    size_t len = strnlen(str, MAX_PATH_LEN);
    char *out = CALLOC(MD5_HASH_LEN + 1, sizeof(char));

    MD5_Init(&c);
    MD5_Update(&c, str, len);
    MD5_Final(md5, &c);

    for(int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        sprintf(out + 2 * i, "%02x", md5[i]);
    }
    return out;
}

void *CALLOC(size_t nmemb, size_t size)
{
    void *ptr = calloc(nmemb, size);
    if (!ptr) {
        fprintf(stderr, "calloc() failed, %s!\n", strerror(errno));
        exit_failure();
    }
    return ptr;
}

