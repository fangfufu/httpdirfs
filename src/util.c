#include "config.h"
#include "util.h"
#include "log.h"

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

void PTHREAD_MUTEX_UNLOCK(pthread_mutex_t * x)
{
        int i;
        i = pthread_mutex_unlock(x);
        if (i) {
                lprintf(fatal,
                        "thread %x: pthread_mutex_unlock() failed, %d, %s\n",
                        pthread_self(), i, strerror(i));
        }
}

void PTHREAD_MUTEX_LOCK(pthread_mutex_t * x)
{
        int i;
        i = pthread_mutex_lock(x);
        if (i) {
                lprintf(fatal,
                        "thread %x: pthread_mutex_lock() failed, %d, %s\n",
                        pthread_self(), i, strerror(i));
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

void erase_string(FILE * file, size_t max_len, char *s)
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

        for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
                sprintf(out + 2 * i, "%02x", md5[i]);
        }
        return out;
}

void *CALLOC(size_t nmemb, size_t size)
{
        void *ptr = calloc(nmemb, size);
        if (!ptr) {
                lprintf(fatal, "calloc() failed, %s!\n", strerror(errno));
        }
        return ptr;
}

void FREE(void *ptr)
{
        free(ptr);
        ptr = NULL;
}

char *str_to_hex(char *s)
{
        char *hex = CALLOC(strlen(s) * 2 + 1, sizeof(char));
        for (char *c = s, *h = hex; *c; c++, h += 2) {
                sprintf(h, "%x", *c);
        }
        return hex;
}
