#include "cache.h"

long Cache_read(const char *filepath, long offset, long len)
{
}

long Cache_write(const char *filepath, long offset, long len,
                   const uint8_t *content)
{
}

int Cache_create(const char *filepath, long len)
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


long Data_read(Cache *cf, long offset, long len,
                const uint8_t *buf);
{
    if (len == 0) {
        fprintf(stderr, "Data_read(): fseek(): requested to read 0 byte!\n");
        return -1;
    }

    FILE *fp;
    fp = fopen(cf->filepath, "r");

    if (!fp) {
        /* The data file does not exist */
        return -1;
    }

    if (fseek(fp, offset, SEEK_SET) == -1) {
        /* fseek failed */
        fprintf(stderr, "Data_read(): fseek(): %s\n", strerror(errno));
        return -1;
    }

    long byte_read = fread((void*) buf, sizeof(uint8_t), len, fp);
    if (byte_read != len) {
        fprintf(stderr,
                "Data_read(): fread(): requested %ld, returned %llu!\n",
                len, byte_read);
        if (feof(fp)) {
            fprintf(stderr,
                    "Data_read(): fread(): reached the end of the file!\n");
        }
        if (ferror(fp)) {
            fprintf(stderr,
                    "Data_read(): fread(): encountered error (from ferror)!\n");
        }
    }
    if (fclose(fp)) {
        fprintf(stderr, "Data_read(): fclose(): %s\n", strerror(errno));
        return -1;
    }
}

long Data_write(Cache *cf, long offset, long len,
                 const uint8_t *buf);
{
}

int Data_create(Cache *cf, long len)
{
    int fd;
    int mode;

    mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    fd = open(cf->filepath, O_WRONLY | O_CREAT, mode);
    if (fd == -1) {
        fprintf(stderr, "Data_create(): %s\n", strerror(errno));
        return 0;
    }
    if (ftruncate(fd, cf->len) == -1) {
        fprintf(stderr, "Data_create(): %s\n", strerror(errno));
        return 0;
    }
    if (close(fd) == -1) {
        fprintf(stderr, "Data_create(): %s\n", strerror(errno));
        return 0;
    }
    return 1;
}
