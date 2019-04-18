#include "cache.h"

long Cache_read(const char *filepath, long offset, long len)
{
}

long Cache_write(const char *filepath, long offset, long len,
                   const uint8_t *content)
{
}

Cache *Cache_create(const char *filepath, long len)
{
}




Cache *Cache_open(const char *filepath)
{
}

void Cache_free(Cache *cf)
{
    free(cf->filename);
    free(cf->metapath);
    free(cf->datapath);
    free(cf->seg);
    free(cf);
}

int Meta_read(Cache *cf)
{
    FILE *fp;
    fp = fopen(cf->metapath, "r");

    if (!fp) {
        /* The metadata file does not exist */
        fprintf(stderr, "Meta_read(): fopen(): %s\n", strerror(errno));
        return -1;
    }

    fread((void *) &(cf->len), sizeof(long), 1, fp);
    fread((void *) &(cf->nseg), sizeof(int), 1, fp);

    /* Allocate some memory for the segment */
    cf->seg = malloc(cf->nseg * sizeof(Seg));

    /* Read all the segment */
    fread((void *) cf->seg, sizeof(Seg), cf->nseg, fp);

    /* Simplified error checking */
    if (ferror(fp)) {
        fprintf(stderr,
                "Meta_read(): fread(): encountered error (from ferror)!\n");
    }

    if (fclose(fp)) {
        fprintf(stderr, "Meta_read(): fclose(): %s\n", strerror(errno));
        return -1;
    }
}

int Meta_write(const Cache *cf)
{
    FILE *fp;
    fp = fopen(cf->metapath, "w");

    if (!fp) {
        /* The metadata file does not exist */
        fprintf(stderr, "Meta_write(): fopen(): %s\n", strerror(errno));
        return -1;
    }

    /* We log the length of the file just in case */
    fwrite((const void *) cf->len, sizeof(long), 1, fp);
    fwrite((const void *) cf->nseg, sizeof(int), 1, fp);

    /* Finally write segments to the file */
    fwrite((const void *) cf->seg, sizeof(Seg), cf->nseg, fp);

    /* Simplified error checking */
    if (ferror(fp)) {
        fprintf(stderr,
                "Meta_write(): fwrite(): encountered error (from ferror)!\n");
    }

    if (fclose(fp)) {
        fprintf(stderr, "Meta_write(): fclose(): %s\n", strerror(errno));
        return -1;
    }
}

long Data_read(const Cache *cf, long offset, long len,
                uint8_t *buf);
{
    if (len == 0) {
        fprintf(stderr, "Data_read(): requested to read 0 byte!\n");
        return -1;
    }

    FILE *fp;
    fp = fopen(cf->datapath, "r");

    if (!fp) {
        /* The data file does not exist */
        fprintf(stderr, "Data_read(): fopen(): %s\n", strerror(errno));
        return -1;
    }

    if (fseek(fp, offset, SEEK_SET) == -1) {
        /* fseek failed */
        fprintf(stderr, "Data_read(): fseek(): %s\n", strerror(errno));
        return -1;
    }

    long byte_read = fread((void *) buf, sizeof(uint8_t), len, fp);
    if (byte_read != len) {
        fprintf(stderr,
                "Data_read(): fread(): requested %ld, returned %llu!\n",
                len, byte_read);
        if (feof(fp)) {
            /* reached EOF */
            fprintf(stderr,
                    "Data_read(): fread(): reached the end of the file!\n");
        }
        if (ferror(fp)) {
            /* filesystem error */
            fprintf(stderr,
                    "Data_read(): fread(): encountered error (from ferror)!\n");
        }
    }
    if (fclose(fp)) {
        fprintf(stderr, "Data_read(): fclose(): %s\n", strerror(errno));
        return -1;
    }
}

long Data_write(const Cache *cf, long offset, long len,
                 const uint8_t *buf);
{
    if (len == 0) {
        fprintf(stderr, "Data_write(): requested to write 0 byte!\n");
        return -1;
    }

    FILE *fp;
    fp = fopen(cf->datapath, "r+");

    if (!fp) {
        /* The data file does not exist */
        return -1;
    }

    if (fseek(fp, offset, SEEK_SET) == -1) {
        /* fseek failed */
        fprintf(stderr, "Data_write(): fseek(): %s\n", strerror(errno));
        return -1;
    }

    long byte_written = fwrite((const void *) buf, sizeof(uint8_t), len, fp);
    if (byte_written != len) {
        fprintf(stderr,
                "Data_write(): fwrite(): requested %ld, returned %llu!\n",
                len, byte_written);
        if (feof(fp)) {
            /* reached EOF */
            fprintf(stderr,
                    "Data_write(): fwrite(): reached the end of the file!\n");
        }
        if (ferror(fp)) {
            /* filesystem error */
            fprintf(stderr,
                "Data_write(): fwrite(): encountered error (from ferror)!\n");
        }
    }
    if (fclose(fp)) {
        fprintf(stderr, "Data_write(): fclose(): %s\n", strerror(errno));
        return -1;
    }
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
