#include "util.h"

#include <execinfo.h>
#include <unistd.h>

#include <stdlib.h>
#include <string.h>

#define BT_BUF_SIZE 100

char *path_append(const char *path, const char *filename)
{
    int needs_separator = 0;
    if ((path[strnlen(path, MAX_PATH_LEN)-1] != '/') && (filename[0] != '/')) {
        needs_separator = 1;
    }

    char *str;
    size_t ul = strnlen(path, MAX_PATH_LEN);
    size_t sl = strnlen(filename, MAX_FILENAME_LEN);
    str = calloc(ul + sl + needs_separator + 1, sizeof(char));
    if (!str) {
        fprintf(stderr, "path_append(): calloc failure!\n");
        exit_failure();
    }
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
