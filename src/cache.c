#include "cache.h"

off_t Cache_read(const char *filepath, off_t offset, size_t size)
{
}

size_t Cache_write(const char *filepath, off_t offset, size_t size,
                   const uint8_t *content)
{
}

int Cache_create(const char *filepath, size_t size)
{
}


Cache *Cache_open(const char *filepath)
{
}


int Meta_write(Cache *cf)
{
}


int Meta_read(Cache *cf)
{
}


size_t Data_read(Cache *cf, off_t offset, size_t size)
{
}

size_t Data_write(Cache *cf, off_t offset, size_t size,
                  const uint8_t *content)
{
}

int Data_create(Cache *cf, size_t size)
{
    int fd;
    int mode;

    mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    fd = open(cf->filepath, O_WRONLY | O_CREAT, mode);
    if (fd == -1) {
        fprintf(stderr, "Data_create(): %s\n", strerror(errno));
        return 0;
    }
    if (ftruncate(fd, cf->size) == -1) {
        fprintf(stderr, "Data_create(): %s\n", strerror(errno));
        return 0;
    }
    if (close(fd) == -1) {
        fprintf(stderr, "Data_create(): %s\n", strerror(errno));
        return 0;
    }
    return 1;
}
