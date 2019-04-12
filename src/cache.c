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
}
