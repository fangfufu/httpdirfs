#include "cache.h"

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

/**
 * \brief strndup with concatenation
 * \details This function creates a new string, and concatenate string a and
 * string b together.
 * \note The maximum length of string a and string b is half the length of the
 * maximum length of the output string.
 * \param[in] a the first string
 * \param[in] b the second string
 * \param[n] c the maximum length of the output string
 */
char *strndupcat(const char *a, const char *b, int n)
{
    int na = strnlen(a, n/2);
    int nb = strnlen(b, n/2);
    int nc = na + nb + 1;
    char *c = calloc(nc, sizeof(char));
    if (!c) {
        fprintf(stderr, "strndupcat(): calloc failure!\n");
    }
    strncpy(c, a, na);
    strncat(c, b, nb);
    return c;
}

void Cache_init(const char *dir)
{
    META_DIR = strndupcat(dir, "meta/", MAX_PATH_LEN);
    DATA_DIR = strndupcat(dir, "data/", MAX_PATH_LEN);
}

Cache *Cache_create(const char *fn, long len)
{
}


Cache *Cache_open(const char *fn)
{
    Cache *cf = malloc(sizeof(Cache));
    char *metafn = strndupcat(META_DIR, fn);
    char *datafn = strndupcat(DATA_DIR, fn);

    /* Check if both metadata and the data file exist */
    if(access(cf->metafn, F_OK ) && access(cf->datafn, F_OK)) {
        return NULL;
    }

    free(metafn);
    free(datafn);
}



long Cache_read(const char *fn, long offset, long len, uint8_t *buf)
{
}

long Cache_write(const char *fn, long offset, long len,
                   const uint8_t *buf)
{
}

void Cache_free(Cache *cf)
{
    free(cf->filename);
    free(cf->seg);
    free(cf);
}

int Meta_read(Cache *cf)
{
    FILE *fp;
    char *metafn = strndupcat(META_DIR, cf->filename, MAX_PATH_LEN);
    fp = fopen(metafn, "r");
    free(metafn);
    int res = 1;

    if (!fp) {
        /* The metadata file does not exist */
        fprintf(stderr, "Meta_read(): fopen(): %s\n", strerror(errno));
        return 0;
    }

    fread(&(cf->len), sizeof(long), 1, fp);
    fread(&(cf->time), sizeof(long), 1, fp);
    fread(&(cf->nseg), sizeof(int), 1, fp);

    /* Allocate some memory for the segment */
    cf->seg = malloc(cf->nseg * sizeof(Seg));

    /* Read all the segment */
    int nmemb = fread(cf->seg, sizeof(Seg), cf->nseg, fp);

    /* Error checking for fread */
    if (ferror(fp)) {
        fprintf(stderr,
                "Meta_read(): fread(): encountered error (from ferror)!\n");
        res = 0;
    }

    /* Check for inconsistent metadata file */
    if (nmemb != cf-> nseg) {
        fprintf(stderr,
                "Meta_read(): corrupted metadata!\n");
        res = 0;
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
    int res = 1;

    if (!fp) {
        /* Cannot create the metadata file */
        fprintf(stderr, "Meta_write(): fopen(): %s\n", strerror(errno));
        return 0;
    }

    fwrite(&(cf->len), sizeof(long), 1, fp);
    fwrite(&(cf->time), sizeof(long), 1, fp);
    fwrite(&(cf->nseg), sizeof(int), 1, fp);

    /* Finally write segments to the file */
    fwrite(cf->seg, sizeof(Seg), cf->nseg, fp);

    /* Error checking for fwrite */
    if (ferror(fp)) {
        fprintf(stderr,
                "Meta_write(): fwrite(): encountered error (from ferror)!\n");
        res = 0;
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
        return 0;
    }
    if (ftruncate(fd, cf->len) == -1) {
        fprintf(stderr, "Data_create(): ftruncate(): %s\n", strerror(errno));
    }
    if (close(fd) == -1) {
        fprintf(stderr, "Data_create(): close:(): %s\n", strerror(errno));
    }
    return 1;
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


