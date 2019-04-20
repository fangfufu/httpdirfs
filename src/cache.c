#include "cache.h"

#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAX_PATH_LEN     4096

/**
 * \brief The metadata directory
 */
char *META_DIR;

/**
 * \brief The data directory
 */
char *DATA_DIR;

void Cache_init(const char *dir)
{
    META_DIR = strndupcat(dir, "meta/", MAX_PATH_LEN);
    DATA_DIR = strndupcat(dir, "data/", MAX_PATH_LEN);
}

Cache *Cache_alloc()
{
    Cache *cf = malloc(sizeof(Cache));
    if (!cf) {
        fprintf(stderr, "Cache_new(): malloc failure!\n");
        exit(EXIT_FAILURE);
    }
    cf->filename = NULL;
    cf->len = 0;
    cf->time = 0;
    cf->nseg = 0;
    cf->seg = NULL;
    return cf;
}

void Cache_free(Cache *cf)
{
    if (cf->filename) {
        free(cf->filename);
    }
    if (cf->seg) {
        free(cf->seg);
    }
    free(cf);
}

int Cache_exist(const char *fn)
{
    int meta_exists = 1;
    int data_exists = 1;
    char *metafn = strndupcat(META_DIR, fn, MAX_PATH_LEN);
    char *datafn = strndupcat(DATA_DIR, fn, MAX_PATH_LEN);

    if (access(metafn, F_OK)) {
        fprintf(stderr, "Cache_exist(): access(): %s\n", strerror(errno));
        meta_exists = 0;
    }

    if (access(datafn, F_OK)) {
        fprintf(stderr, "Cache_exist(): access(): %s\n", strerror(errno));
        data_exists = 0;
    }

    if (meta_exists ^ data_exists) {
        if (meta_exists) {
            if(unlink(metafn)) {
                fprintf(stderr, "Cache_exist(): unlink(): %s\n",
                        strerror(errno));
            }
        }
        if (data_exists) {
            if(unlink(datafn)) {
                fprintf(stderr, "Cache_exist(): unlink(): %s\n",
                        strerror(errno));
            }
        }
    }

    free(metafn);
    free(datafn);

    return 1;
}

Cache *Cache_create(const char *fn, long len, long time)
{
    Cache *cf = Cache_alloc();

    cf->filename = strndup(fn, MAX_PATH_LEN);
    cf->len = len;
    cf->time = time;

    if (Data_create(cf)) {
        Cache_free(cf);
        fprintf(stderr, "Cache_create(): Data_create() failed!\n");
        return NULL;
    }

    if (Meta_create(cf)) {
        Cache_free(cf);
        fprintf(stderr, "Cache_create(): Meta_create() failed!\n");
        return NULL;
    }
    return cf;
}

Cache *Cache_open(const char *fn)
{
    /* Create the cache in-memory data structure */
    Cache *cf = Cache_alloc();
    cf->filename = strndup(fn, MAX_PATH_LEN);

    if (Meta_read(cf)) {
        Cache_free(cf);
        return NULL;
    }

    if (cf->len != Data_size(fn)) {
        fprintf(stderr,
            "Cache_open(): metadata is inconsistent with the data file!\n");
        Cache_free(cf);
        return NULL;
    }
    return cf;
}

int Meta_create(const Cache *cf)
{
    return Meta_write(cf);
}

int Meta_read(Cache *cf)
{
    FILE *fp;
    char *metafn = strndupcat(META_DIR, cf->filename, MAX_PATH_LEN);
    fp = fopen(metafn, "r");
    free(metafn);
    int res = 0;
    int nmemb = 0;

    if (!fp) {
        /* The metadata file does not exist */
        fprintf(stderr, "Meta_read(): fopen(): %s\n", strerror(errno));
        return -1;
    }

    fread(&(cf->len), sizeof(long), 1, fp);
    fread(&(cf->time), sizeof(long), 1, fp);
    fread(&(cf->nseg), sizeof(int), 1, fp);

    if (cf->nseg) {
        /* Allocate some memory for the segment */
        cf->seg = malloc(cf->nseg * sizeof(Seg));
        if (!(cf->seg)) {
            fprintf(stderr, "Meta_read(): malloc failure!\n");
            exit(EXIT_FAILURE);
        }
        /* Read all the segment */
        nmemb = fread(cf->seg, sizeof(Seg), cf->nseg, fp);
    }

    /* Error checking for fread */
    if (ferror(fp)) {
        fprintf(stderr,
                "Meta_read(): fread(): encountered error (from ferror)!\n");
        res = -1;
    }

    /* Check for inconsistent metadata file */
    if (nmemb != cf-> nseg) {
        fprintf(stderr,
                "Meta_read(): corrupted metadata!\n");
        res = -1;
    }

    if (fclose(fp)) {
        fprintf(stderr, "Meta_read(): fclose(): %s\n", strerror(errno));
    }

    return res;
}

int Meta_write(const Cache *cf)
{
    FILE *fp;
    char *metafn = strndupcat(META_DIR, cf->filename, MAX_PATH_LEN);
    fp = fopen(metafn, "w");
    free(metafn);
    int res = 0;

    if (!fp) {
        /* Cannot create the metadata file */
        fprintf(stderr, "Meta_write(): fopen(): %s\n", strerror(errno));
        return -1;
    }

    fwrite(&(cf->len), sizeof(long), 1, fp);
    fwrite(&(cf->time), sizeof(long), 1, fp);
    fwrite(&(cf->nseg), sizeof(int), 1, fp);

    /* Finally write segments to the file */
    if (cf->nseg) {
        fwrite(cf->seg, sizeof(Seg), cf->nseg, fp);
    }

    /* Error checking for fwrite */
    if (ferror(fp)) {
        fprintf(stderr,
                "Meta_write(): fwrite(): encountered error (from ferror)!\n");
        res = -1;
    }

    if (fclose(fp)) {
        fprintf(stderr, "Meta_write(): fclose(): %s\n", strerror(errno));
    }
    return res;
}

int Data_create(Cache *cf)
{
    int fd;
    int mode;

    mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    char *datafn = strndupcat(DATA_DIR, cf->filename, MAX_PATH_LEN);
    fd = open(datafn, O_WRONLY | O_CREAT, mode);
    free(datafn);
    if (fd == -1) {
        fprintf(stderr, "Data_create(): open(): %s\n", strerror(errno));
        return -1;
    }
    if (ftruncate(fd, cf->len) == -1) {
        fprintf(stderr, "Data_create(): ftruncate(): %s\n", strerror(errno));
    }
    if (close(fd)) {
        fprintf(stderr, "Data_create(): close:(): %s\n", strerror(errno));
    }
    return 0;
}

long Data_size(const char *fn)
{
    char *datafn = strndupcat(DATA_DIR, fn, MAX_PATH_LEN);
    struct stat st;
    if (stat(datafn, &st) == 0) {
        free(datafn);
        return st.st_blksize;
    }
    free(datafn);
    fprintf(stderr, "Data_size(): stat(): %s\n", strerror(errno));
    return -1;
}

long Data_read(const Cache *cf, long offset, long len,
               uint8_t *buf)
{
    if (len == 0) {
        fprintf(stderr, "Data_read(): requested to read 0 byte!\n");
        return -1;
    }

    FILE *fp;
    char *datafn = strndupcat(DATA_DIR, cf->filename, MAX_PATH_LEN);
    fp = fopen(datafn, "r");
    free(datafn);
    long byte_read = -1;

    if (!fp) {
        /* Failed to open the data file */
        fprintf(stderr, "Data_read(): fopen(): %s\n", strerror(errno));
        return -1;
    }

    if (fseek(fp, offset, SEEK_SET) == -1) {
        /* fseek failed */
        fprintf(stderr, "Data_read(): fseek(): %s\n", strerror(errno));
        goto cleanup;
    }

    byte_read = fread(buf, sizeof(uint8_t), len, fp);
    if (byte_read != len) {
        fprintf(stderr,
                "Data_read(): fread(): requested %ld, returned %ld!\n",
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

    cleanup:
    if (fclose(fp)) {
        fprintf(stderr, "Data_read(): fclose(): %s\n", strerror(errno));
    }
    return byte_read;
}

long Data_write(const Cache *cf, long offset, long len,
                const uint8_t *buf)
{
    if (len == 0) {
        fprintf(stderr, "Data_write(): requested to write 0 byte!\n");
        return -1;
    }

    FILE *fp;
    char *datafn = strndupcat(DATA_DIR, cf->filename, MAX_PATH_LEN);
    fp = fopen(datafn, "r+");
    free(datafn);
    long byte_written = -1;

    if (!fp) {
        /* Failed to open the data file */
        fprintf(stderr, "Data_write(): fopen(): %s\n", strerror(errno));
        return -1;
    }

    if (fseek(fp, offset, SEEK_SET) == -1) {
        /* fseek failed */
        fprintf(stderr, "Data_write(): fseek(): %s\n", strerror(errno));
        goto cleanup;
    }

    byte_written = fwrite(buf, sizeof(uint8_t), len, fp);
    if (byte_written != len) {
        fprintf(stderr,
                "Data_write(): fwrite(): requested %ld, returned %ld!\n",
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

    cleanup:
    if (fclose(fp)) {
        fprintf(stderr, "Data_write(): fclose(): %s\n", strerror(errno));
    }
    return byte_written;
}






