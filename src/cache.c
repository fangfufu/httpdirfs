/*
 * HTTPDirFS - HTTP Directory Filesystem
 *
 * Copyright (C) 2020-2026 Fufu Fang <fangfufu2003@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the OpenSSL
 * library.
 */

/**
 * \file cache.c
 * \brief Permanent cache system implementation
 */

#include "cache.h"

#include "config.h"
#include "link.h"
#include "log.h"
#include "transfer.h"
#include "url.h"
#include "util.h"
#include <curl/curl.h>

#include <sys/param.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * ---------------- External variables -----------------------
 */
int CACHE_SYSTEM_INIT = 0;
char *CACHE_DIR;

/*
 * ----------------- Static variables -----------------------
 */

/**
 * \brief Cache file locking
 * \details Ensure cache opening and cache closing is an atomic operation
 */
static pthread_mutex_t cf_lock;

/**
 * \brief Whether CACHE_DIR was allocated by this module
 * \details When the user supplies --cache-location, CACHE_DIR points to
 * CONFIG.cache_dir which is freed by Config_cleanup().
 */
static int cache_dir_owned = 0;


char *CacheSystem_get_cache_dir(void)
{
    if (CONFIG.cache_dir) {
        return STRDUP(CONFIG.cache_dir);
    }

    const char *default_cache_subdir = "/.cache";
    char *cache_dir = NULL;

    const char *xdg_cache_home = getenv("XDG_CACHE_HOME");
    if (xdg_cache_home) {
        cache_dir = STRNDUP(xdg_cache_home, PATH_MAX);
    } else {
        const char *user_home = getenv("HOME");
        if (user_home) {
            cache_dir = path_append(user_home, default_cache_subdir);
        } else {
            lprintf(warning, "$HOME is unset\n");
            /*
             * XDG_CACHE_HOME and HOME already are full paths. Not relying
             * on environment PWD since it too may be undefined.
             */
            char *cur_dir = REALPATH("./", NULL);
            if (cur_dir) {
                cache_dir = path_append(cur_dir, default_cache_subdir);
                FREE(cur_dir);
            } else {
                lprintf(fatal, "Could not create cache directory\n");
            }
        }
    }
    return cache_dir;
}

char *CacheSystem_get_cache_root(void)
{
    char *cache_home = CacheSystem_get_cache_dir();
    char *root;
    if (CONFIG.cache_dir) {
        /*
         * A custom cache location is used verbatim as the cache root of the
         * mounted server.
         */
        root = cache_home;
    } else {
        root = path_append(cache_home, "/httpdirfs/");
        FREE(cache_home);
    }
    return root;
}

/**
 * \brief Compute the cache directory path of a server origin without
 * creating anything on disk.
 * \note The caller must free the returned string with FREE().
 */
static char *cache_host_path(const char *url)
{
    char *cache_dir_root = CacheSystem_get_cache_root();
    char *server_root = get_server_root(url);
    const char *target_url = server_root ? server_root : url;
    CURL *c = curl_easy_init();
    char *escaped_url = curl_easy_escape(c, target_url, 0);
    char *full_path = path_append(cache_dir_root, escaped_url);
    FREE(cache_dir_root);
    if (server_root) {
        FREE(server_root);
    }
    curl_free(escaped_url);
    curl_easy_cleanup(c);
    return full_path;
}

char *CacheSystem_calc_dir(const char *url)
{
    char *cache_home = CacheSystem_get_cache_dir();

    if (CONFIG.cache_dir) {
        /*
         * A custom cache location is the cache root of this server itself;
         * no origin directory is appended.
         */
        if (mkdir(cache_home, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
            && (errno != EEXIST)) {
            lprintf(fatal, "mkdir(): %s\n", strerror(errno));
        }
        return cache_home;
    }

    if (mkdir(cache_home, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
        && (errno != EEXIST)) {
        lprintf(fatal, "mkdir(): %s\n", strerror(errno));
    }
    char *cache_dir_root = path_append(cache_home, "/httpdirfs/");
    if (mkdir(cache_dir_root, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
        && (errno != EEXIST)) {
        lprintf(fatal, "mkdir(): %s\n", strerror(errno));
    }
    FREE(cache_home);

    char *fn = path_append(cache_dir_root, "/CACHEDIR.TAG");
    FILE *fp = fopen(fn, "w");
    if (fp) {
        fprintf(fp, "Signature: 8a477f597d28d172789f06886806bc55\n\
# This file is a cache directory tag created by httpdirfs.\n\
# For information about cache directory tags, see:\n\
#	http://www.brynosaurus.com/cachedir/\n");
    } else {
        lprintf(fatal, "fopen(%s): %s", fn, strerror(errno));
    }
    if (ferror(fp)) {
        lprintf(fatal, "fwrite(): encountered error!\n");
    }
    if (fclose(fp)) {
        lprintf(fatal, "fclose(%s): %s\n", fn, strerror(errno));
    }
    FREE(fn);

    char *full_path = cache_host_path(url);
    if (mkdir(full_path, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
        && (errno != EEXIST)) {
        lprintf(fatal, "mkdir(): %s\n", strerror(errno));
    }
    return full_path;
}

void CacheSystem_init(const char *path, int url_supplied)
{
    lprintf(cache_lock_debug, "thread %lx: initialise cf_lock;\n",
            (unsigned long)pthread_self());
    PTHREAD_MUTEX_INIT(&cf_lock, NULL);

    if (url_supplied) {
        CACHE_DIR = CacheSystem_calc_dir(path);
    } else {
        CACHE_DIR = STRDUP(path);
        if (mkdir(CACHE_DIR, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
            && (errno != EEXIST)) {
            lprintf(fatal, "mkdir(): %s\n", strerror(errno));
        }
    }
    cache_dir_owned = 1;

    CACHE_SYSTEM_INIT = 1;
}

void CacheSystem_cleanup(void)
{
    if (CACHE_SYSTEM_INIT) {
        if (cache_dir_owned) {
            FREE(CACHE_DIR);
            cache_dir_owned = 0;
        }
        PTHREAD_MUTEX_DESTROY(&cf_lock);
        CACHE_SYSTEM_INIT = 0;
    }
}

static int ntfw_cb(const char *fpath, const struct stat *sb, int typeflag,
                   struct FTW *ftwbuf)
{
    (void)sb;
    (void)typeflag;
    (void)ftwbuf;
    return remove(fpath);
}

void CacheSystem_clear(void)
{
    char *cache_del = CacheSystem_get_cache_root();
    nftw(cache_del, ntfw_cb, 64, FTW_DEPTH | FTW_PHYS | FTW_MOUNT);
    if (!CONFIG.cache_dir) {
        FREE(cache_del);
    }
    exit(EXIT_SUCCESS);
}

int CacheSystem_delete_host(const char *arg)
{
    if (!arg || !arg[0]) {
        lprintf(error, "--cache-clear-host requires a URL or host\n");
        return -1;
    }

    if (CONFIG.cache_dir) {
        lprintf(error,
                "--cache-clear-host is not supported together with "
                "--cache-location: per-origin subdirectories only exist in "
                "the default cache location. Use --cache-clear to remove "
                "the custom cache directory instead.\n");
        return -1;
    }

    static const char *schemes[2] = {"https", "http"};
    int found = 0;

    if (strstr(arg, "://")) {
        /*
         * A full URL was supplied: only the escaped server root directory of
         * that origin is removed.
         */
        char *host_dir = cache_host_path(arg);
        struct stat st;
        if (stat(host_dir, &st) == 0) {
            found = 1;
        }
        nftw(host_dir, ntfw_cb, 64, FTW_DEPTH | FTW_PHYS | FTW_MOUNT);
        if (remove(host_dir) && errno != ENOENT) {
            lprintf(warning, "remove(%s): %s\n", host_dir, strerror(errno));
        }
        FREE(host_dir);
    } else {
        /*
         * A bare host was supplied: check both the https and the http origin
         * directories.
         */
        for (int i = 0; i < 2; i++) {
            char *url = CALLOC(PATH_MAX + 1, sizeof(char));
            snprintf(url, PATH_MAX + 1, "%s://%s", schemes[i], arg);
            char *host_dir = cache_host_path(url);
            struct stat st;
            if (stat(host_dir, &st) == 0) {
                found = 1;
            }
            nftw(host_dir, ntfw_cb, 64, FTW_DEPTH | FTW_PHYS | FTW_MOUNT);
            if (remove(host_dir) && errno != ENOENT) {
                lprintf(warning, "remove(%s): %s\n", host_dir, strerror(errno));
            }
            FREE(host_dir);
            FREE(url);
        }
    }
    return found;
}

void CacheSystem_clear_host(const char *arg)
{
    int rc = CacheSystem_delete_host(arg);
    exit(rc < 0 ? EXIT_FAILURE : EXIT_SUCCESS);
}

static void ensure_parent_dir(const char *filepath);

/**
 * \brief Return the canonical source string used as the cache key of a link
 * \details For SONIC mode the stable track id is used (the stream URL
 * contains per-session authentication tokens), for all other modes the
 * canonical f_url is used.
 */
static const char *cache_key_source(const Link *link)
{
    if (CONFIG.mode == SONIC && link->sonic.id) {
        return link->sonic.id;
    }
    return link->f_url;
}

static char *resolve_redirect_fn(const char *fn, int depth)
{
    if (!fn || depth > 5 || !CACHE_DIR) {
        return fn ? STRDUP(fn) : NULL;
    }
    char *full_path = path_append(CACHE_DIR, fn);
    FILE *fp = fopen(full_path, "r");
    FREE(full_path);
    if (!fp) {
        return STRDUP(fn);
    }
    CacheHeader hdr;
    if (fread(&hdr, 1, CACHE_HEADER_SIZE, fp) != CACHE_HEADER_SIZE) {
        fclose(fp);
        return STRDUP(fn);
    }
    if (hdr.magic != CACHE_MAGIC || hdr.version != CACHE_VERSION
        || !(hdr.flags & CACHE_FLAG_IS_REDIRECT)) {
        fclose(fp);
        return STRDUP(fn);
    }
    if (fseeko(fp, (off_t)hdr.header_size, SEEK_SET) != 0) {
        fclose(fp);
        return STRDUP(fn);
    }
    char *target_url = CALLOC((size_t)hdr.content_length + 1, sizeof(char));
    if (fread(target_url, 1, (size_t)hdr.content_length, fp)
        != (size_t)hdr.content_length) {
        FREE(target_url);
        fclose(fp);
        return STRDUP(fn);
    }
    fclose(fp);

    char *target_fn = url_to_cache_path(target_url);
    FREE(target_url);
    if (!target_fn) {
        return STRDUP(fn);
    }
    char *resolved = resolve_redirect_fn(target_fn, depth + 1);
    FREE(target_fn);
    return resolved;
}

/**
 * \brief Derive the hash-sharded cache path ("ab/<hash>") of a link
 * \note The caller must free the returned string with FREE().
 */
static char *cache_key_for_link(const Link *link)
{
    if (!link) {
        return NULL;
    }
    char *initial_fn = NULL;
    if (CONFIG.mode == SONIC && link->sonic.id) {
        initial_fn = string_to_cache_path(link->sonic.id);
    } else {
        initial_fn = url_to_cache_path(cache_key_source(link));
    }
    if (!initial_fn) {
        return NULL;
    }
    char *resolved = resolve_redirect_fn(initial_fn, 0);
    FREE(initial_fn);
    return resolved;
}

/**
 * \brief Compute the on-disk section offsets of a container file
 * \return 0 on success, -1 on overflow
 */
static int container_compute_layout(const char *url, size_t http_header_len,
                                    long segbc, off_t *header_size,
                                    off_t *bitmap_offset,
                                    off_t *http_header_offset)
{
    size_t url_len = strnlen(url, PATH_MAX);
    size_t raw_meta_size
        = CACHE_HEADER_SIZE + url_len + 1 + http_header_len + (size_t)segbc;
    if (raw_meta_size > (size_t)INT32_MAX - CACHE_PAGE_SIZE) {
        return -1;
    }
    /*
     * Align to a 4096-byte boundary so that the payload starts on a
     * filesystem block boundary.
     */
    size_t hs = (raw_meta_size + CACHE_PAGE_SIZE - 1)
                & ~(size_t)(CACHE_PAGE_SIZE - 1);
    *http_header_offset = (off_t)(CACHE_HEADER_SIZE + url_len + 1);
    *bitmap_offset = (off_t)(*http_header_offset + http_header_len);
    *header_size = (off_t)hs;
    return 0;
}

/**
 * \brief Fill in the fixed CacheHeader structure from in-memory state
 */
static void container_fill_header(CacheHeader *hdr, const char *url,
                                  size_t http_header_len, off_t header_size,
                                  int64_t remote_mtime, off_t content_length,
                                  int blksz, long segbc)
{
    memset(hdr, 0, CACHE_HEADER_SIZE);
    hdr->magic = CACHE_MAGIC;
    hdr->version = CACHE_VERSION;
    hdr->flags = CACHE_FLAG_IS_SPARSE;
    hdr->url_len = (uint32_t)strnlen(url, PATH_MAX);
    hdr->header_size = (uint32_t)header_size;
    hdr->http_header_len = (uint32_t)http_header_len;
    hdr->cache_time = (int64_t)time(NULL);
    hdr->remote_mtime = remote_mtime;
    hdr->content_length = content_length;
    hdr->blksz = blksz;
    hdr->segbc = (int32_t)segbc;
}

/**
 * \brief Create the container file of a cache entry
 * \details Opens <CACHE_DIR>/<cf->path> with O_RDWR|O_CREAT|O_TRUNC, writes
 * the CacheHeader, the canonical source URL and a zeroed segment bitmap,
 * pads to the 4KB-aligned payload offset, then ftruncates to
 * header_size + content_length so that the payload region is allocated
 * sparsely.
 * \return 0 on success, -1 on failure
 */
static int Container_create(Cache *cf)
{
    const char *url = cache_key_source(cf->link);
    off_t content_length = (off_t)cf->link->content_length;
    if (content_length < 0
        || (size_t)content_length != cf->link->content_length) {
        lprintf(fatal, "File size too large for system off_t: %zu\n",
                cf->link->content_length);
    }

    char *full_path = path_append(CACHE_DIR, cf->path);
    ensure_parent_dir(full_path);

    /*
     * Check if a HEAD-only container already exists for this URL.
     * If so, preserve its HTTP headers and remote timestamp during promotion.
     */
    char *saved_http_hdr = NULL;
    size_t saved_http_hdr_len = 0;
    int existing_fd = open(full_path, O_RDONLY);
    if (existing_fd != -1) {
        CacheHeader ex_hdr;
        if (read(existing_fd, &ex_hdr, CACHE_HEADER_SIZE)
            == CACHE_HEADER_SIZE) {
            if (ex_hdr.magic == CACHE_MAGIC && ex_hdr.version == CACHE_VERSION
                && (ex_hdr.flags & CACHE_FLAG_IS_HEAD)) {
                if (ex_hdr.http_header_len > 0) {
                    off_t hdr_off
                        = (off_t)(CACHE_HEADER_SIZE + ex_hdr.url_len + 1);
                    if (lseek(existing_fd, hdr_off, SEEK_SET) == hdr_off) {
                        saved_http_hdr = CALLOC((size_t)ex_hdr.http_header_len,
                                                sizeof(char));
                        if (read(existing_fd, saved_http_hdr,
                                 ex_hdr.http_header_len)
                            == (ssize_t)ex_hdr.http_header_len) {
                            saved_http_hdr_len = (size_t)ex_hdr.http_header_len;
                        } else {
                            FREE(saved_http_hdr);
                            saved_http_hdr = NULL;
                        }
                    }
                }
            }
        }
        close(existing_fd);
    }

    off_t header_size;
    off_t bitmap_offset;
    off_t http_header_offset;
    if (container_compute_layout(url, saved_http_hdr_len, cf->segbc,
                                 &header_size, &bitmap_offset,
                                 &http_header_offset)) {
        lprintf(fatal, "container layout overflow for %s\n", cf->path);
    }
    cf->header_size = header_size;
    cf->bitmap_offset = bitmap_offset;
    cf->http_header_offset = http_header_offset;

    int fd = open(full_path, O_RDWR | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
        lprintf(error, "open(%s): %s\n", cf->path, strerror(errno));
        FREE(saved_http_hdr);
        FREE(full_path);
        return -1;
    }
    FREE(full_path);

    cf->fp = fdopen(fd, "r+");
    if (!cf->fp) {
        lprintf(error, "fdopen(): %s\n", strerror(errno));
        close(fd);
        FREE(saved_http_hdr);
        return -1;
    }

    CacheHeader hdr;
    container_fill_header(&hdr, url, saved_http_hdr_len, header_size,
                          (int64_t)cf->link->time, content_length, cf->blksz,
                          cf->segbc);
    hdr.flags |= CACHE_FLAG_IS_SPARSE;

    int ok = 1;
    if (fwrite(&hdr, 1, CACHE_HEADER_SIZE, cf->fp) != CACHE_HEADER_SIZE) {
        ok = 0;
    }
    if (ok
        && fwrite(url, 1, (size_t)hdr.url_len + 1, cf->fp)
               != (size_t)hdr.url_len + 1) {
        ok = 0;
    }
    if (ok && saved_http_hdr_len > 0
        && fwrite(saved_http_hdr, 1, saved_http_hdr_len, cf->fp)
               != saved_http_hdr_len) {
        ok = 0;
    }
    if (ok && cf->segbc > 0
        && fwrite(cf->seg, sizeof(Seg), (size_t)cf->segbc, cf->fp)
               != (size_t)cf->segbc) {
        ok = 0;
    }
    /*
     * Seek to the last padding byte so that the write zero-fills the whole
     * padding region up to the page-aligned payload offset.
     */
    if (ok && fseeko(cf->fp, header_size - 1, SEEK_SET) != 0) {
        ok = 0;
    }
    if (ok && fputc('\0', cf->fp) != '\0') {
        ok = 0;
    }
    if (ok && fflush(cf->fp) != 0) {
        ok = 0;
    }
    if (ok && ftruncate(fd, header_size + content_length) != 0) {
        lprintf(warning, "ftruncate(): %s\n", strerror(errno));
    }
    FREE(saved_http_hdr);

    if (!ok) {
        lprintf(error, "failed to write container file %s\n", cf->path);
        if (fclose(cf->fp)) {
            lprintf(error, "fclose(): %s\n", strerror(errno));
        }
        cf->fp = NULL;
        return -1;
    }
    return 0;
}

/**
 * \brief Read and validate the container header and bitmap of an open cache
 * \details Validates the magic, the version, the canonical source URL, the
 * deterministic timestamps (cache_time against CONFIG.refresh_timeout and
 * remote_mtime against the live Link), the content length and the segment
 * count, then loads the segment bitmap into memory.
 * \return 0 on success, errno on error (EBADMSG for outdated/corrupt files)
 */
static int Container_read(Cache *cf)
{
    FILE *fp = cf->fp;

    if (!fp) {
        lprintf(error, "fopen(): %s\n", strerror(errno));
        return EIO;
    }

    if (fseeko(fp, 0, SEEK_SET) != 0) {
        lprintf(error, "fseeko(): %s\n", strerror(errno));
        return EIO;
    }

    if (!cf->link) {
        lprintf(error, "cf->link is NULL in Container_read\n");
        return EINVAL;
    }

    CacheHeader hdr;
    if (fread(&hdr, 1, CACHE_HEADER_SIZE, fp) != CACHE_HEADER_SIZE
        || ferror(fp)) {
        lprintf(error, "error reading core metadata %s!\n", cf->path);
        return EIO;
    }

    if (hdr.magic != CACHE_MAGIC || hdr.version != CACHE_VERSION) {
        lprintf(error,
                "not a valid cache container: %s (magic: 0x%08x, "
                "version: %u)\n",
                cf->path, hdr.magic, hdr.version);
        return EBADMSG;
    }

    /*
     * We do not support zero-byte files in the on-disk cache files.
     * Both content_length and segbc must be strictly positive.
     */
    if (hdr.content_length <= 0 || hdr.blksz <= 0 || hdr.segbc <= 0) {
        lprintf(error,
                "corruption: content_length: %jd, blksz: %d, segbc: %d\n",
                (intmax_t)hdr.content_length, hdr.blksz, hdr.segbc);
        return EBADMSG;
    }
    if (hdr.header_size < CACHE_PAGE_SIZE
        || hdr.header_size % CACHE_PAGE_SIZE != 0) {
        lprintf(error, "corruption: invalid header_size %u in %s\n",
                hdr.header_size, cf->path);
        return EBADMSG;
    }

    if (hdr.blksz != CONFIG.data_blksz) {
        lprintf(warning, "Warning: cached blksz != CONFIG.data_blksz\n");
    }

    /*
     * Read back the canonical source URL and verify it matches the live
     * link. A mismatch means a (theoretically impossible) hash collision
     * or a corrupt container.
     */
    if (hdr.url_len > PATH_MAX) {
        lprintf(error, "corruption: invalid url_len %u in %s\n", hdr.url_len,
                cf->path);
        return EBADMSG;
    }
    char *disk_url = CALLOC((size_t)hdr.url_len + 1, sizeof(char));
    if (fread(disk_url, 1, hdr.url_len, fp) != hdr.url_len || ferror(fp)) {
        lprintf(error, "error reading URL from %s!\n", cf->path);
        FREE(disk_url);
        return EIO;
    }
    const char *src = cache_key_source(cf->link);
    if (strncmp(src, disk_url, hdr.url_len) != 0 || src[hdr.url_len] != '\0') {
        lprintf(warning, "cache key mismatch in %s, deleting\n", cf->path);
        FREE(disk_url);
        return EBADMSG;
    }
    FREE(disk_url);

    /*
     * A file payload whose remote Last-Modified timestamp and content length
     * both match the live link has not changed on the server, so it stays
     * valid regardless of its age: once a segment of the file has been
     * downloaded, it is not downloaded again. Only when the remote metadata
     * is unknown do we fall back to the deterministic age-based
     * invalidation, which is immune to filesystem timestamp quirks
     * (progressive writes updating st_mtime, cp resetting timestamps, ...).
     * HEAD metadata and directory listings have their own expiry handling.
     */
    int remote_unchanged = hdr.remote_mtime > 0 && cf->link->time > 0
                           && hdr.remote_mtime == (int64_t)cf->link->time
                           && (uintmax_t)hdr.content_length
                                  == (uintmax_t)cf->link->content_length;

    if (!remote_unchanged) {
        int64_t age = (int64_t)time(NULL) - hdr.cache_time;
        if (age > CONFIG.refresh_timeout) {
            lprintf(warning, "outdated cache file: %s (age: %jd, limit: %d)\n",
                    cf->path, (intmax_t)age, CONFIG.refresh_timeout);
            return EBADMSG;
        }
    }

    /*
     * Verify the remote Last-Modified timestamp, if both are known.
     */
    if (hdr.remote_mtime > 0 && cf->link->time > 0
        && hdr.remote_mtime != (int64_t)cf->link->time) {
        lprintf(warning,
                "outdated cache file: %s (disk mtime: %jd, link mtime: %ld)\n",
                cf->path, (intmax_t)hdr.remote_mtime, cf->link->time);
        return EBADMSG;
    }

    if ((uintmax_t)hdr.content_length != (uintmax_t)cf->link->content_length) {
        lprintf(warning, "cache size mismatch: %s (disk: %jd, link: %zu)\n",
                cf->path, (intmax_t)hdr.content_length,
                cf->link->content_length);
        return EBADMSG;
    }

    off_t max_segbc = hdr.content_length / hdr.blksz;
    if (max_segbc >= INT_MAX) {
        max_segbc = INT_MAX;
    } else if (hdr.content_length % hdr.blksz != 0) {
        max_segbc += 1;
    }
    if (hdr.segbc != max_segbc) {
        lprintf(error, "Error: invalid segbc size: %d (expected: %ld)\n",
                hdr.segbc, (long)max_segbc);
        return EBADMSG;
    }

    /*
     * Remember the section offsets, then skip to the bitmap and load it.
     */
    cf->header_size = (off_t)hdr.header_size;
    cf->http_header_offset = (off_t)(CACHE_HEADER_SIZE + hdr.url_len + 1);
    cf->bitmap_offset = (off_t)(cf->http_header_offset + hdr.http_header_len);

    if (fseeko(fp, cf->bitmap_offset, SEEK_SET) != 0) {
        lprintf(error, "fseeko(): %s\n", strerror(errno));
        return EIO;
    }
    cf->segbc = hdr.segbc;
    cf->seg = CALLOC((size_t)cf->segbc, sizeof(Seg));
    if (fread(cf->seg, sizeof(Seg), (size_t)cf->segbc, fp)
        != (size_t)cf->segbc) {
        lprintf(error, "corrupted metadata!\n");
        FREE(cf->seg);
        cf->seg = NULL;
        return EBADMSG;
    }
    if (ferror(fp)) {
        lprintf(error, "error reading bitmap!\n");
        FREE(cf->seg);
        cf->seg = NULL;
        return EIO;
    }

    return 0;
}

/**
 * \brief Persist the current segment bitmap to the container file
 * \note Must be called while holding cf->w_lock.
 * \return 0 on success, -1 on failure
 */
static int Container_write_bitmap(Cache *cf)
{
    if (!cf->fp || cf->segbc <= 0 || !cf->seg) {
        return -1;
    }
    if (fseeko(cf->fp, cf->bitmap_offset, SEEK_SET) != 0) {
        lprintf(error, "fseeko(): %s\n", strerror(errno));
        return -1;
    }
    if (fwrite(cf->seg, sizeof(Seg), (size_t)cf->segbc, cf->fp)
        != (size_t)cf->segbc) {
        lprintf(error, "fwrite(): encountered error!\n");
        return -1;
    }
    if (fflush(cf->fp) != 0) {
        lprintf(error, "fflush(): encountered error!\n");
        return -1;
    }
    return 0;
}

static void ensure_parent_dir(const char *filepath)
{
    if (!filepath) {
        return;
    }
    char tmp[PATH_MAX];
    strncpy(tmp, filepath, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *last_slash = strrchr(tmp, '/');
    if (last_slash && last_slash != tmp) {
        *last_slash = '\0';
        (void)mkdir_p(tmp, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    }
}

/**
 * \brief read a data file
 * \param[in] cf the pointer to the cache in-memory data structure
 * \param[out] buf the output buffer
 * \param[in] len the length of the segment
 * \param[in] offset the offset of the segment
 * \return
 *  - negative values on error,
 *  - otherwise, the number of bytes read.
 */
static long Data_read(Cache *cf, uint8_t *buf, off_t len, off_t offset)
{
    if (!cf->link) {
        lprintf(error, "cf->link is NULL in Data_read\n");
        return -EINVAL;
    }

    off_t total_len = (off_t)cf->link->content_length;
    if (total_len < 0 || (size_t)total_len != cf->link->content_length) {
        lprintf(error, "content_length overflow in Data_read\n");
        return -EINVAL;
    }

    if (len < 0) {
        lprintf(error, "requested to read negative bytes: %jd\n",
                (intmax_t)len);
        return -EINVAL;
    }

    if (len == 0) {
        lprintf(error, "requested to read 0 byte!\n");
        return -EINVAL;
    }

    lprintf(cache_lock_debug, "thread %lx: locking seek_lock;\n",
            (unsigned long)pthread_self());
    PTHREAD_MUTEX_LOCK(&cf->seek_lock);

    long byte_read = 0;

    if (offset < 0 || offset >= total_len) {
        goto end;
    } else if (len > total_len - offset) {
        len = total_len - offset;
    }

    /*
     * Seek to the right location (the payload starts at cf->header_size)
     */
    if (fseeko(cf->fp, cf->header_size + offset, SEEK_SET)) {
        /*
         * fseeko failed
         */
        lprintf(error, "fseeko(): %s\n", strerror(errno));
        byte_read = -EIO;
        goto end;
    }

    byte_read = fread(buf, sizeof(uint8_t), (size_t)len, cf->fp);
    if (byte_read != len) {
        if (feof(cf->fp)) {
            /*
             * reached EOF
             */
            lprintf(error, "fread(): reached the end of the file!\n");
        }
        if (ferror(cf->fp)) {
            /*
             * filesystem error
             */
            lprintf(error, "fread(): encountered error!\n");
        }
    }

end:

    lprintf(cache_lock_debug, "thread %lx: unlocking seek_lock;\n",
            (unsigned long)pthread_self());
    PTHREAD_MUTEX_UNLOCK(&cf->seek_lock);
    return byte_read;
}

/**
 * \brief write to a data file
 * \param[in] cf the pointer to the cache in-memory data structure
 * \param[in] buf the input buffer
 * \param[in] len the length of the segment
 * \param[in] offset the offset of the segment
 * \return
 *  - -1 when the data file does not exist
 *  - otherwise, the number of bytes written.
 */
static long Data_write(Cache *cf, const uint8_t *buf, off_t len, off_t offset)
{
    if (len <= 0) {
        lprintf(error, "requested to write 0 or negative bytes: %jd\n",
                (intmax_t)len);
        return -EINVAL;
    }

    lprintf(cache_lock_debug, "thread %lx: locking seek_lock;\n",
            (unsigned long)pthread_self());
    PTHREAD_MUTEX_LOCK(&cf->seek_lock);

    long byte_written = 0;

    /*
     * The payload starts at cf->header_size
     */
    if (fseeko(cf->fp, cf->header_size + offset, SEEK_SET)) {
        /*
         * fseeko failed
         */
        lprintf(error, "fseeko(): %s\n", strerror(errno));
        byte_written = -EIO;
        goto end;
    }

    byte_written = fwrite(buf, sizeof(uint8_t), (size_t)len, cf->fp);

    if (byte_written != len) {
        lprintf(error, "fwrite(): requested %ld, returned %ld!\n", len,
                byte_written);
    }

    if (ferror(cf->fp)) {
        /*
         * filesystem error
         */
        lprintf(error, "fwrite(): encountered error!\n");
    }

end:
    lprintf(cache_lock_debug, "thread %lx: unlocking seek_lock;\n",
            (unsigned long)pthread_self());
    PTHREAD_MUTEX_UNLOCK(&cf->seek_lock);
    return byte_written;
}

ActiveDownload *ActiveDownload_find(Cache *cf, off_t offset)
{
    ActiveDownload *ad = cf->active_dls;
    while (ad) {
        if (ad->offset == offset) {
            return ad;
        }
        ad = ad->next;
    }
    return NULL;
}

/**
 * \brief Allocates and prepends a new ActiveDownload tracker to the list.
 * \param[in] cf The cache instance.
 * \param[in] offset The offset to track.
 * \note Must be called while holding cf->dl_lock.
 */
static void ActiveDownload_add(Cache *cf, off_t offset)
{
    ActiveDownload *ad = CALLOC(1, sizeof(ActiveDownload));
    ad->offset = offset;
    ad->ts = NULL;
    PTHREAD_COND_INIT(&ad->cond, NULL);
    ad->refcount = 1;
    ad->next = cf->active_dls;
    cf->active_dls = ad;
}

void ActiveDownload_unref(ActiveDownload *ad)
{
    if (ad == NULL) {
        return;
    }
    ad->refcount--;
    if (ad->refcount == 0) {
        PTHREAD_COND_DESTROY(&ad->cond);
        FREE(ad);
    }
}

/**
 * \brief Removes and frees the ActiveDownload tracker for the given offset.
 * \param[in] cf The cache instance.
 * \param[in] offset The offset to untrack.
 * \note Must be called while holding cf->dl_lock.
 */
static void ActiveDownload_remove(Cache *cf, off_t offset)
{
    ActiveDownload **curr = &cf->active_dls;
    while (*curr) {
        if ((*curr)->offset == offset) {
            ActiveDownload *temp = *curr;
            *curr = (*curr)->next;
            temp->unlinked = 1;
            PTHREAD_COND_BROADCAST(&temp->cond);
            ActiveDownload_unref(temp);
            return;
        }
        curr = &(*curr)->next;
    }
}

/**
 * \brief Decrement the waiter/active thread count under dl_lock and broadcast
 * if shutting down and no waiters remain.
 * \param[in] cf The cache instance.
 */
static void Cache_waiter_decrement(Cache *cf)
{
    cf->waiters--;
    if (cf->shutting_down && cf->waiters == 0) {
        PTHREAD_COND_BROADCAST(&cf->shutdown_cond);
    }
}

/**
 * \brief Allocate a new cache data structure
 */
static Cache *Cache_alloc(void)
{
    Cache *cf = CALLOC(1, sizeof(Cache));
    PTHREAD_MUTEX_INIT(&cf->seek_lock, NULL);
    PTHREAD_MUTEX_INIT(&cf->w_lock, NULL);
    PTHREAD_MUTEX_INIT(&cf->dl_lock, NULL);
    cf->active_dls = NULL;
    cf->cache_opened = 1;
    cf->waiters = 0;
    PTHREAD_COND_INIT(&cf->shutdown_cond, NULL);
    cf->shutting_down = 0;

    cf->num_bg_workers = MIN(CONFIG.max_conns, DEFAULT_NETWORK_MAX_CONNS) / 2;
    if (cf->num_bg_workers <= 0) {
        cf->num_bg_workers = 1;
    }

    SEM_INIT(&cf->bgt_sem, 0, cf->num_bg_workers);
    return cf;
}

/**
 * \brief free a cache data structure
 */
static void Cache_free(Cache *cf)
{
    if (cf->path) {
        FREE(cf->path);
    }

    if (cf->seg) {
        FREE(cf->seg);
    }

    PTHREAD_MUTEX_LOCK(&cf->dl_lock);
    cf->shutting_down = 1;
    ActiveDownload *ad = cf->active_dls;
    cf->active_dls = NULL;
    while (ad) {
        ActiveDownload *next = ad->next;
        if (ad->refcount > 1) {
            lprintf(warning,
                    "Cache_free: ActiveDownload at offset %jd has refcount %d "
                    "(transfer or waiters still active)\n",
                    (intmax_t)ad->offset, ad->refcount);
        }
        ad->next = NULL;
        ad->unlinked = 1;
        PTHREAD_COND_BROADCAST(&ad->cond);
        ActiveDownload_unref(ad);
        ad = next;
    }
    while (cf->waiters > 0) {
        PTHREAD_COND_WAIT(&cf->shutdown_cond, &cf->dl_lock);
    }
    PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);

    PTHREAD_MUTEX_DESTROY(&cf->seek_lock);
    PTHREAD_MUTEX_DESTROY(&cf->w_lock);
    PTHREAD_MUTEX_DESTROY(&cf->dl_lock);
    PTHREAD_COND_DESTROY(&cf->shutdown_cond);
    SEM_DESTROY(&cf->bgt_sem);

    FREE(cf);
}

/**
 * \brief Check if the container file of a cache entry exists and is usable
 * \details A container file whose size does not even cover the fixed
 * CacheHeader (e.g. it was created but the header was never written) is
 * treated as missing and deleted.
 * \return
 *  -   0, if the container file exists
 *  -   -1, otherwise
 */
static int Cache_exist(const char *fn)
{
    char *full_path = path_append(CACHE_DIR, fn);
    int res = -1;
    int fd = open(full_path, O_RDONLY);
    if (fd != -1) {
        struct stat st;
        if (fstat(fd, &st) == 0) {
            if (st.st_size >= CACHE_HEADER_SIZE) {
                CacheHeader hdr;
                if (pread(fd, &hdr, CACHE_HEADER_SIZE, 0)
                    == (ssize_t)CACHE_HEADER_SIZE) {
                    if (hdr.magic == CACHE_MAGIC && hdr.version == CACHE_VERSION
                        && (hdr.flags
                            & (CACHE_FLAG_IS_COMPLETE
                               | CACHE_FLAG_IS_SPARSE))) {
                        int64_t age = (int64_t)time(NULL) - hdr.cache_time;
                        if (age <= CONFIG.refresh_timeout) {
                            res = 0;
                        }
                    }
                }
            } else {
                lprintf(warning, "Cache file partially missing or invalid "
                                 "(zero-length).\n");
                if (unlink(full_path) && errno != ENOENT) {
                    lprintf(fatal, "unlink(): %s\n", strerror(errno));
                }
            }
        }
        close(fd);
    }
    FREE(full_path);
    return res;
}

/**
 * \brief delete a cache file
 */
void Cache_delete(const char *fn)
{
    Link *link = path_to_Link(fn);
    if (!link) {
        return;
    }
    char *cache_key = cache_key_for_link(link);
    if (!cache_key) {
        lprintf(error, "Failed to derive cache key\n");
        LinkTable_unref(link->parent_table);
        return;
    }
    char *full_path = path_append(CACHE_DIR, cache_key);
    if (unlink(full_path) && errno != ENOENT) {
        lprintf(error, "unlink(): %s\n", strerror(errno));
    }
    FREE(full_path);
    FREE(cache_key);
    LinkTable_unref(link->parent_table);
}

/**
 * \brief Open the container file of a cache data set
 * \return
 *  -   0 on success
 *  -   -1 on failure, with appropriate errno set.
 */
static int Container_open(Cache *cf)
{
    char *full_path = path_append(CACHE_DIR, cf->path);
    cf->fp = fopen(full_path, "r+");
    if (!cf->fp) {
        /*
         * Failed to open the container file
         */
        lprintf(error, "fopen(%s): %s\n", cf->path, strerror(errno));
        FREE(full_path);
        return -1;
    }
    FREE(full_path);
    return 0;
}

int Cache_create(const char *path)
{
    Link *this_link = path_to_Link(path);
    if (!this_link) {
        return 1;
    }

    if (this_link->content_length <= 0) {
        lprintf(error, "Zero-length files are not supported in cache system\n");
        LinkTable_unref(this_link->parent_table);
        return 1;
    }

    char *fn = cache_key_for_link(this_link);
    if (!fn) {
        lprintf(error, "Failed to derive cache key from URL\n");
        LinkTable_unref(this_link->parent_table);
        return 1;
    }

    Cache *cf = Cache_alloc();
    cf->path = STRNDUP(fn, PATH_MAX);
    cf->link = this_link;
    cf->blksz = CONFIG.data_blksz;
    size_t calculated_segbc = this_link->content_length / cf->blksz;
    if (calculated_segbc >= INT_MAX) {
        cf->segbc = INT_MAX;
    } else {
        cf->segbc = (long)calculated_segbc;
        if (this_link->content_length % cf->blksz != 0) {
            cf->segbc += 1;
        }
    }
    cf->seg = CALLOC(cf->segbc, sizeof(Seg));

    int res = Container_create(cf);
    if (res) {
        lprintf(error, "Container_create() failed for %s\n", path);
    }
    if (cf->fp && fclose(cf->fp)) {
        lprintf(error, "cannot close container after write, %s.\n",
                strerror(errno));
    }
    cf->fp = NULL;

    lprintf(cache_lock_debug, "Flushing cache file for %s after creating.\n",
            path);
    Cache_free(cf);

    res = Cache_exist(fn);

    if (res) {
        lprintf(fatal, "Cache file creation failed for %s\n", path);
    }

    FREE(fn);
    LinkTable_unref(this_link->parent_table);
    return res;
}

Cache *Cache_open(const char *fn)
{
    /*
     * Obtain the link structure memory pointer
     */
    Link *link = path_to_Link(fn);
    if (!link) {
        /*
         * There is no associated link to the path
         */
        return NULL;
    }

    lprintf(cache_lock_debug, "thread %lx: locking cf_lock;\n",
            (unsigned long)pthread_self());
    PTHREAD_MUTEX_LOCK(&cf_lock);

    if (link->cache_ptr) {
        link->cache_ptr->cache_opened++;
        lprintf(cache_lock_debug, "thread %lx: unlocking cf_lock;\n",
                (unsigned long)pthread_self());
        PTHREAD_MUTEX_UNLOCK(&cf_lock);
        LinkTable_unref(link->parent_table);
        return link->cache_ptr;
    }

    char *actual_fn = cache_key_for_link(link);
    if (!actual_fn) {
        lprintf(error, "Failed to derive cache path from URL: %s\n",
                link->f_url);
        lprintf(cache_lock_debug, "thread %lx: unlocking cf_lock;\n",
                (unsigned long)pthread_self());
        PTHREAD_MUTEX_UNLOCK(&cf_lock);
        LinkTable_unref(link->parent_table);
        return NULL;
    }

    if (link->content_length <= 0) {
        lprintf(error, "Zero-length files are not supported in cache system\n");
        lprintf(cache_lock_debug, "thread %lx: unlocking cf_lock;\n",
                (unsigned long)pthread_self());
        PTHREAD_MUTEX_UNLOCK(&cf_lock);
        LinkTable_unref(link->parent_table);
        FREE(actual_fn);
        return NULL;
    }

    // Try up to 2 times. If opening fails on the first attempt (outdated,
    // corrupt, etc.), we delete the cache and try creating and opening a fresh
    // one.
    for (int attempt = 0; attempt < 2; attempt++) {
        if (Cache_exist(actual_fn) != 0) {
            if (attempt > 0) {
                Cache_delete(fn);
            }
            if (Cache_create(fn) != 0) {
                lprintf(cache_lock_debug, "thread %lx: unlocking cf_lock;\n",
                        (unsigned long)pthread_self());
                PTHREAD_MUTEX_UNLOCK(&cf_lock);
                LinkTable_unref(link->parent_table);
                FREE(actual_fn);
                return NULL;
            }
        }

        /*
         * Create the cache in-memory data structure
         */
        Cache *cf = Cache_alloc();

        cf->path = STRNDUP(actual_fn, PATH_MAX);

        /*
         * Associate the cache structure with a link
         */
        cf->link = link;
        cf->blksz = CONFIG.data_blksz;

        int ok = 1;
        if (Container_open(cf)) {
            lprintf(error, "cannot open container file %s.\n", actual_fn);
            ok = 0;
        } else if (Container_read(cf)) {
            lprintf(error, "metadata error: %s.\n", actual_fn);
            ok = 0;
        } else {
            char *full_path = path_append(CACHE_DIR, actual_fn);
            struct stat st;
            if (stat(full_path, &st) != 0) {
                lprintf(error, "cannot stat container file %s.\n", actual_fn);
                ok = 0;
            } else if ((uintmax_t)cf->link->content_length
                       > (uintmax_t)st.st_size - (uintmax_t)cf->header_size) {
                lprintf(error,
                        "metadata inconsistency %s, "
                        "cf->link->content_length: %zu, container size: %jd.\n",
                        actual_fn, cf->link->content_length,
                        (intmax_t)st.st_size);
                ok = 0;
            }
            FREE(full_path);
        }

        if (ok) {
            /*
             * Yup, we just created a circular loop. ;)
             */
            cf->link->cache_ptr = cf;

            lprintf(cache_lock_debug, "thread %lx: unlocking cf_lock;\n",
                    (unsigned long)pthread_self());
            PTHREAD_MUTEX_UNLOCK(&cf_lock);
            FREE(actual_fn);
            return cf;
        }

        // Clean up opened resources before retry
        if (cf->fp) {
            fclose(cf->fp);
            cf->fp = NULL;
        }
        Cache_free(cf);
        Cache_delete(fn);
    }

    lprintf(cache_lock_debug, "thread %lx: unlocking cf_lock;\n",
            (unsigned long)pthread_self());
    PTHREAD_MUTEX_UNLOCK(&cf_lock);
    LinkTable_unref(link->parent_table);
    FREE(actual_fn);
    return NULL;
}

void Cache_close(Cache *cf)
{
    lprintf(cache_lock_debug, "thread %lx: locking cf_lock: %s\n",
            (unsigned long)pthread_self(), cf->path);
    PTHREAD_MUTEX_LOCK(&cf_lock);

    cf->cache_opened--;

    if (cf->cache_opened > 0) {
        lprintf(cache_lock_debug,
                "thread %lx: unlocking cf_lock: %s, cache_opened: %d\n",
                (unsigned long)pthread_self(), cf->path, cf->cache_opened);
        PTHREAD_MUTEX_UNLOCK(&cf_lock);
        return;
    }

    /*
     * Wait for any background download to finish before closing. If we don't
     * wait, Cache_free() might be called while Cache_bgdl() is still
     * running. This will cause a use-after-free error.
     */
    lprintf(cache_lock_debug,
            "thread %lx: waiting for background download to finish for %s\n",
            (unsigned long)pthread_self(), cf->path);
    for (int i = 0; i < cf->num_bg_workers; i++) {
        SEM_WAIT(&cf->bgt_sem);
    }

    /*
     * Persist the final bitmap so that a restarted process does not
     * re-download the segments that are already on disk. The CacheHeader is
     * left untouched: cache_time must keep recording the download time for
     * deterministic timestamp invalidation.
     */
    PTHREAD_MUTEX_LOCK(&cf->w_lock);
    if (cf->fp && Container_write_bitmap(cf)) {
        lprintf(error, "Container_write_bitmap() error.");
    }
    PTHREAD_MUTEX_UNLOCK(&cf->w_lock);

    if (cf->fp && fclose(cf->fp)) {
        lprintf(error, "cannot close container file %s.\n", strerror(errno));
    }
    cf->fp = NULL;

    Link *link = cf->link;
    link->cache_ptr = NULL;

    lprintf(cache_lock_debug,
            "thread %lx: unlocking cf_lock, cache closed: %s\n",
            (unsigned long)pthread_self(), cf->path);
    Cache_free(cf);
    LinkTable_unref(link->parent_table);
    PTHREAD_MUTEX_UNLOCK(&cf_lock);
}

/**
 * \brief Check if a segment exists.
 * \return 1 if the segment exists
 */
static int Seg_exist(Cache *cf, off_t offset)
{
    if (cf->segbc <= 0 || cf->blksz <= 0) {
        return 0;
    }
    off_t byte = offset / cf->blksz;
    if (byte < 0 || byte >= cf->segbc) {
        return 0;
    }
    return cf->seg[byte];
}

/**
 * \brief Set the existence of a segment
 * \param[in] cf the cache in-memory data structure
 * \param[in] offset the starting position of the segment.
 * \param[in] i 1 for exist, 0 for doesn't exist
 * \note Call this after downloading a segment.
 */
static void Seg_set(Cache *cf, off_t offset, int i)
{
    if (cf->segbc <= 0 || cf->blksz <= 0) {
        return;
    }
    off_t byte = offset / cf->blksz;
    if (byte < 0 || byte >= cf->segbc) {
        return;
    }
    cf->seg[byte] = i;
}

/**
 * \brief Arguments passed to the background download thread.
 */
typedef struct BgdlArg {
    Cache *cf;       /**< The cache instance. */
    off_t dl_offset; /**< The segment offset to download. */
} BgdlArg;

/**
 * \brief Background download function
 * \details If we are requesting the data from the second half of the current
 * segment, we can spawn a pthread using this function to download the next
 * segment.
 * \param[in] arg A pointer to a BgdlArg structure.
 */
static void *Cache_bgdl(void *arg)
{
    BgdlArg *bg_arg = (BgdlArg *)arg;
    Cache *cf = bg_arg->cf;
    off_t dl_offset = bg_arg->dl_offset;
    FREE(bg_arg);

    uint8_t *recv_buf = CALLOC(cf->blksz, sizeof(uint8_t));
    long recv
        = Link_download(cf->link, (char *)recv_buf, cf->blksz, dl_offset, cf);
    if (recv < 0) {
        lprintf(error,
                "thread %lx received %ld bytes, "
                "which doesn't make sense\n",
                (unsigned long)pthread_self(), recv);
        FREE(recv_buf);
        PTHREAD_MUTEX_LOCK(&cf->dl_lock);
        ActiveDownload_remove(cf, dl_offset);
        Cache_waiter_decrement(cf);
        PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
        SEM_POST(&cf->bgt_sem);
        pthread_exit(NULL);
    }

    PTHREAD_MUTEX_LOCK(&cf->w_lock);
    if ((recv == cf->blksz)
        || ((uintmax_t)dl_offset
            == (cf->link->content_length / (size_t)cf->blksz
                * (size_t)cf->blksz))) {
        if (Data_write(cf, recv_buf, recv, dl_offset) == recv) {
            Seg_set(cf, dl_offset, 1);
            /*
             * Persist the bitmap so that a crashed process does not
             * re-download segments that are already on disk.
             */
            (void)Container_write_bitmap(cf);
        }
    } else {
        lprintf(error,
                "received %ld rather than %d, possible network "
                "error.\n",
                recv, cf->blksz);
    }
    PTHREAD_MUTEX_UNLOCK(&cf->w_lock);

    FREE(recv_buf);

    PTHREAD_MUTEX_LOCK(&cf->dl_lock);
    ActiveDownload_remove(cf, dl_offset);
    Cache_waiter_decrement(cf);
    PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);

    SEM_POST(&cf->bgt_sem);

    pthread_exit(NULL);
}

/**
 * \brief Spawns a background thread to download the next segment.
 * \param[in] cf The cache instance.
 * \param[in] dl_offset The offset of the segment to download.
 */
static void Cache_bgdl_launcher(Cache *cf, off_t dl_offset)
{
    pthread_t thread;
    pthread_attr_t attr;

    if (pthread_attr_init(&attr)) {
        lprintf(fatal, "pthread_attr_init():%d, %s\n", errno, strerror(errno));
    }

    if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) {
        lprintf(fatal, "pthread_attr_setdetachstate():%d, %s\n", errno,
                strerror(errno));
    }

    BgdlArg *arg = CALLOC(1, sizeof(BgdlArg));
    arg->cf = cf;
    arg->dl_offset = dl_offset;

    PTHREAD_MUTEX_LOCK(&cf->dl_lock);
    cf->waiters++;
    PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);

    if (pthread_create(&thread, &attr, Cache_bgdl, arg)) {
        PTHREAD_MUTEX_LOCK(&cf->dl_lock);
        Cache_waiter_decrement(cf);
        PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
        FREE(arg);
        lprintf(fatal, "pthread_create(): %d, %s\n", errno, strerror(errno));
    }

    if (pthread_attr_destroy(&attr)) {
        lprintf(fatal, "pthread_attr_destroy(): %d, %s\n", errno,
                strerror(errno));
    }
}

/**
 * \brief Reads a segment of size 'len' starting at 'offset_start'.
 * \param[in] cf The cache instance.
 * \param[out] output_buf Output buffer to read data into.
 * \param[in] len Length of the data to read.
 * \param[in] offset_start The offset to start reading from.
 * \return The number of bytes read, or negative on error.
 *
 * \details Concurrency Architecture & State Machine:
 * ----------------------------------------
 * This function supports multiple concurrent segment downloads to allow
 * parallel FUSE reading. To prevent race conditions, memory leaks, and
 * duplicate downloads:
 *
 * 1. Mutual Exclusion & Locking:
 *    - `w_lock` guards writing to the cache files (`dfp`/`mfp`) and checking
 * segment existence.
 *    - `dl_lock` guards access to `active_dls`, which tracks currently active
 * downloads.
 *    - Ordering: ALWAYS lock `w_lock` before `dl_lock` to avoid deadlocks.
 *
 * 2. Active Download Tracking (`active_dls`):
 *    - A linked list of `ActiveDownload` nodes tracks the offsets currently
 * being downloaded.
 *    - If a segment is not cached but is already being downloaded by another
 * thread, subsequent FUSE threads will detect the node and wait via
 * `PTHREAD_COND_WAIT` on `ad->cond`.
 *
 * 3. Early-Return Copy:
 *    - While waiting, threads can perform early returns by copying data
 * directly from the in-progress `TransferStruct`'s memory buffer (`ts->data`)
 * once enough bytes have been received.
 *
 * 4. Double-Checked Locking:
 *    - Because locks must be released when launching threads or checking
 * semaphores, other threads could concurrently insert download trackers. We use
 * double-checked locking inside the background thread launcher and `sync_dl`
 * fallback path to verify that the download is still not tracked before
 * allocating a new node.
 */
static long Cache_read_segment(Cache *cf, char *const output_buf,
                               const off_t len, const off_t offset_start)
{
    if (!cf->link) {
        lprintf(error, "cf->link is NULL in Cache_read_segment\n");
        return -EINVAL;
    }

    if (cf->link->content_length <= 0 || offset_start < 0
        || (size_t)offset_start >= cf->link->content_length) {
        return 0;
    }

    long send;
    off_t dl_offset = offset_start / cf->blksz * cf->blksz;
    int ret;

retry:
    PTHREAD_MUTEX_LOCK(&cf->w_lock);
    if (Seg_exist(cf, dl_offset)) {
        send = Data_read(cf, (uint8_t *)output_buf, len, offset_start);
        PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
        goto bgdl;
    }

    PTHREAD_MUTEX_LOCK(&cf->dl_lock);
    ActiveDownload *ad = ActiveDownload_find(cf, dl_offset);
    if (ad != NULL) {
        PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
        ad->refcount++;
        ActiveDownload *orig_ad = ad;

        cf->waiters++;

        while (!cf->shutting_down && !orig_ad->unlinked) {
            if (orig_ad->ts && orig_ad->ts->data
                && orig_ad->ts->curr_size
                       >= (size_t)(offset_start - dl_offset + len)) {
                memcpy(output_buf,
                       orig_ad->ts->data + (offset_start - dl_offset), len);

                Cache_waiter_decrement(cf);

                ActiveDownload_unref(orig_ad);
                PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
                send = len;
                goto bgdl;
            }
            PTHREAD_COND_WAIT(&orig_ad->cond, &cf->dl_lock);
        }

        int was_shutdown = cf->shutting_down;
        Cache_waiter_decrement(cf);

        ActiveDownload_unref(orig_ad);
        PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);

        if (was_shutdown) {
            return -EIO;
        }

        PTHREAD_MUTEX_LOCK(&cf->w_lock);
        if (Seg_exist(cf, dl_offset)) {
            send = Data_read(cf, (uint8_t *)output_buf, len, offset_start);
            PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
            goto bgdl;
        }
        goto sync_dl;
    }
    PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);

    /*
     * Attempt to launch a background download thread if the background thread
     * slot is available (bgt_sem > 0). This acts as a throttle on concurrent
     * background prefetches.
     */
    ret = SEM_TRYWAIT(&cf->bgt_sem);
    if (ret == 0) {
        PTHREAD_MUTEX_LOCK(&cf->dl_lock);
        /*
         * Double-checked locking: Re-verify that another thread hasn't already
         * added this offset to the active downloads list while we were
         * unlocked.
         */
        ActiveDownload *bg_ad = ActiveDownload_find(cf, dl_offset);
        if (bg_ad == NULL) {
            ActiveDownload_add(cf, dl_offset);
            PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
            Cache_bgdl_launcher(cf, dl_offset);
            PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
            goto retry;
        }
        PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
        SEM_POST(&cf->bgt_sem); /* Back off and release the slot */
    }

sync_dl:
    PTHREAD_MUTEX_LOCK(&cf->dl_lock);
    /*
     * Double-checked locking: Verify that another thread hasn't concurrently
     * started a download for this offset. If it has, back off, release the
     * locks, and retry to join the wait loop.
     */
    ActiveDownload *sync_ad = ActiveDownload_find(cf, dl_offset);
    if (sync_ad != NULL) {
        PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
        PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
        goto retry;
    }
    ActiveDownload_add(cf, dl_offset);
    cf->waiters++;
    PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);

    PTHREAD_MUTEX_UNLOCK(&cf->w_lock);

    uint8_t *recv_buf = CALLOC(cf->blksz, sizeof(uint8_t));
    long recv
        = Link_download(cf->link, (char *)recv_buf, cf->blksz, dl_offset, cf);

    PTHREAD_MUTEX_LOCK(&cf->w_lock);

    if (recv < 0) {
        PTHREAD_MUTEX_LOCK(&cf->dl_lock);
        ActiveDownload_remove(cf, dl_offset);
        Cache_waiter_decrement(cf);
        PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
        FREE(recv_buf);
        PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
        return recv;
    }
    if ((recv == cf->blksz)
        || ((uintmax_t)dl_offset
            == (cf->link->content_length / (size_t)cf->blksz
                * (size_t)cf->blksz))) {
        if (recv < (offset_start - dl_offset) + len) {
            lprintf(error,
                    "received %ld bytes, but required at least %ld bytes\n",
                    recv, (long)((offset_start - dl_offset) + len));
            PTHREAD_MUTEX_LOCK(&cf->dl_lock);
            ActiveDownload_remove(cf, dl_offset);
            Cache_waiter_decrement(cf);
            PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
            FREE(recv_buf);
            PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
            return -EIO;
        }
        if (Data_write(cf, recv_buf, recv, dl_offset) == recv) {
            Seg_set(cf, dl_offset, 1);
            /*
             * Persist the bitmap so that a crashed process does not
             * re-download segments that are already on disk.
             */
            (void)Container_write_bitmap(cf);
        }
    } else {
        lprintf(error,
                "received %ld rather than %d, possible network "
                "error.\n",
                recv, cf->blksz);
        PTHREAD_MUTEX_LOCK(&cf->dl_lock);
        ActiveDownload_remove(cf, dl_offset);
        Cache_waiter_decrement(cf);
        PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
        FREE(recv_buf);
        PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
        return -EIO;
    }

    PTHREAD_MUTEX_LOCK(&cf->dl_lock);
    ActiveDownload_remove(cf, dl_offset);
    Cache_waiter_decrement(cf);
    PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
    send = len;
    if (offset_start < dl_offset
        || (size_t)(offset_start - dl_offset) + (size_t)send
               > (size_t)cf->blksz) {
        lprintf(error, "invalid offset or length for memcpy, aborting copy\n");
        send = 0;
    } else {
        memcpy(output_buf, recv_buf + (offset_start - dl_offset), send);
    }
    FREE(recv_buf);
    PTHREAD_MUTEX_UNLOCK(&cf->w_lock);

bgdl: {
}
    off_t next_dl_offset = dl_offset + cf->blksz;
    int next_seg_missing = 0;
    if ((uintmax_t)next_dl_offset < (uintmax_t)cf->link->content_length) {
        PTHREAD_MUTEX_LOCK(&cf->w_lock);
        next_seg_missing = !Seg_exist(cf, next_dl_offset);
        PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
    }
    if (next_seg_missing) {
        ret = SEM_TRYWAIT(&cf->bgt_sem);
        if (!ret) {
            PTHREAD_MUTEX_LOCK(&cf->w_lock);
            next_seg_missing = !Seg_exist(cf, next_dl_offset);
            PTHREAD_MUTEX_LOCK(&cf->dl_lock);
            const ActiveDownload *next_ad
                = ActiveDownload_find(cf, next_dl_offset);
            if (next_seg_missing && next_ad == NULL) {
                ActiveDownload_add(cf, next_dl_offset);
                PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
                PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
                Cache_bgdl_launcher(cf, next_dl_offset);
            } else {
                PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
                PTHREAD_MUTEX_UNLOCK(&cf->w_lock);
                SEM_POST(&cf->bgt_sem);
            }
        }
    }

    return send;
}

long Cache_read(Cache *cf, char *const output_buf, off_t len,
                const off_t offset_start)
{
    if (!cf || !cf->link) {
        lprintf(error, "Invalid cache or link in Cache_read\n");
        return -EINVAL;
    }

    if (len < 0) {
        lprintf(error, "requested to read negative bytes: %jd\n",
                (intmax_t)len);
        return -EINVAL;
    }

    if (offset_start < 0 || (size_t)offset_start >= cf->link->content_length) {
        return 0;
    }

    size_t remaining = cf->link->content_length - (size_t)offset_start;
    if ((size_t)len > remaining) {
        len = (off_t)remaining;
    }

    if (len <= 0) {
        return 0;
    }

    off_t send = 0;
    for (off_t start = offset_start, end; len > 0;
         len -= end - start, start = end) {
        end = start / cf->blksz * cf->blksz + cf->blksz;
        if (end > start + len) {
            end = start + len;
        }
        long seg_send = Cache_read_segment(
            cf, output_buf + (start - offset_start), end - start, start);
        if (seg_send < 0) {
            return seg_send;
        }
        send += seg_send;
        if (seg_send < end - start) {
            break;
        }
    }
    return send;
}

/**
 * \brief Write a fresh container file for a cached directory listing
 * \details Stores the raw HTTP response headers and the HTML payload in a
 * single container file at "<CACHE_DIR>/<shard>/<hash>", using the
 * CacheHeader binary format. The payload is complete (the whole HTML page),
 * so every segment of the bitmap is marked as present.
 * \return 0 on success, -1 on failure
 */
int CacheContainer_write(const char *url, const char *payload,
                         size_t payload_len, const char *http_header,
                         size_t http_header_len)
{
    if (!CACHE_SYSTEM_INIT || !url || !payload || payload_len == 0) {
        lprintf(error, "invalid arguments to CacheContainer_write\n");
        return -1;
    }

    char *canon_url = canonicalize_url(url);
    const char *key_url = canon_url ? canon_url : url;

    char *fn = string_to_cache_path(key_url);
    if (!fn) {
        lprintf(error, "Failed to derive cache path from URL: %s\n", url);
        FREE(canon_url);
        return -1;
    }

    int blksz = CONFIG.data_blksz;
    size_t segbc = payload_len / (size_t)blksz;
    if (segbc >= INT_MAX) {
        segbc = INT_MAX;
    } else if (payload_len % (size_t)blksz != 0) {
        segbc += 1;
    }

    off_t header_size;
    off_t bitmap_offset;
    off_t http_header_offset;
    if (container_compute_layout(key_url, http_header_len, (long)segbc,
                                 &header_size, &bitmap_offset,
                                 &http_header_offset)) {
        lprintf(error, "container layout overflow for %s\n", fn);
        FREE(fn);
        FREE(canon_url);
        return -1;
    }

    char *full_path = path_append(CACHE_DIR, fn);
    ensure_parent_dir(full_path);
    int fd = open(full_path, O_RDWR | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
        lprintf(error, "open(%s): %s\n", fn, strerror(errno));
        FREE(full_path);
        FREE(fn);
        FREE(canon_url);
        return -1;
    }
    FILE *fp = fdopen(fd, "r+");
    if (!fp) {
        lprintf(error, "fdopen(): %s\n", strerror(errno));
        close(fd);
        FREE(full_path);
        FREE(fn);
        FREE(canon_url);
        return -1;
    }

    CacheHeader hdr;
    container_fill_header(&hdr, key_url, http_header_len, header_size, 0,
                          (off_t)payload_len, blksz, (long)segbc);
    hdr.flags |= CACHE_FLAG_IS_COMPLETE | CACHE_FLAG_IS_DIR;

    int ok = 1;
    if (fwrite(&hdr, 1, CACHE_HEADER_SIZE, fp) != CACHE_HEADER_SIZE) {
        ok = 0;
    }
    if (ok
        && fwrite(key_url, 1, (size_t)hdr.url_len + 1, fp)
               != (size_t)hdr.url_len + 1) {
        ok = 0;
    }
    if (ok && http_header_len > 0
        && fwrite(http_header, 1, http_header_len, fp) != http_header_len) {
        ok = 0;
    }
    /*
     * The payload is complete: every segment is present.
     */
    const Seg seg_full = 1;
    for (size_t i = 0; i < segbc && ok; i++) {
        if (fwrite(&seg_full, sizeof(Seg), 1, fp) != 1) {
            ok = 0;
        }
    }
    if (ok && fseeko(fp, header_size - 1, SEEK_SET) != 0) {
        ok = 0;
    }
    if (ok && fputc('\0', fp) != '\0') {
        ok = 0;
    }
    if (ok && fwrite(payload, 1, payload_len, fp) != payload_len) {
        ok = 0;
    }
    if (ok && fflush(fp) != 0) {
        ok = 0;
    }
    if (ok && ferror(fp)) {
        ok = 0;
    }

    if (!ok) {
        lprintf(error, "failed to write container file %s\n", fn);
        (void)fclose(fp);
        if (unlink(full_path) && errno != ENOENT) {
            lprintf(error, "unlink(): %s\n", strerror(errno));
        }
    } else if (fclose(fp)) {
        lprintf(error, "fclose(%s): %s\n", fn, strerror(errno));
    }
    FREE(full_path);
    FREE(fn);
    FREE(canon_url);
    return ok ? 0 : -1;
}

int CacheContainer_delete(const char *url)
{
    if (!CACHE_SYSTEM_INIT || !url || !url[0]) {
        return 0;
    }

    char *canon_url = canonicalize_url(url);
    const char *key_url = canon_url ? canon_url : url;

    char *fn = string_to_cache_path(key_url);
    if (!fn) {
        FREE(canon_url);
        return -1;
    }

    char *full_path = path_append(CACHE_DIR, fn);
    int res = 0;
    if (unlink(full_path) && errno != ENOENT) {
        lprintf(error, "unlink(%s): %s\n", fn, strerror(errno));
        res = -1;
    }
    FREE(full_path);
    FREE(fn);
    FREE(canon_url);
    return res;
}

static int CacheContainer_read_internal(const char *url, char **out_payload,
                                        size_t *out_payload_len,
                                        char **out_http_header,
                                        size_t *out_http_header_len,
                                        time_t *out_cache_time,
                                        char **out_resolved_url, int depth)
{
    *out_payload = NULL;
    *out_payload_len = 0;
    *out_http_header = NULL;
    *out_http_header_len = 0;
    if (out_cache_time) {
        *out_cache_time = 0;
    }
    if (out_resolved_url) {
        *out_resolved_url = NULL;
    }

    if (!CACHE_SYSTEM_INIT || !url || !url[0] || depth > 5) {
        return 0;
    }

    char *canon_url = canonicalize_url(url);
    const char *key_url = canon_url ? canon_url : url;

    char *fn = string_to_cache_path(key_url);
    if (!fn) {
        FREE(canon_url);
        return 0;
    }

    char *full_path = path_append(CACHE_DIR, fn);
    FILE *fp = fopen(full_path, "r");
    if (!fp) {
        lprintf(debug, "cache container not found for %s (%s)\n", url, fn);
        FREE(full_path);
        FREE(fn);
        FREE(canon_url);
        return 0;
    }

    int res = -1;
    CacheHeader hdr;
    struct stat cst;
    if (fread(&hdr, 1, CACHE_HEADER_SIZE, fp) != CACHE_HEADER_SIZE
        || ferror(fp)) {
        lprintf(error, "corrupt container file %s\n", fn);
    } else if (hdr.magic != CACHE_MAGIC || hdr.version != CACHE_VERSION) {
        lprintf(error, "not a valid cache container: %s\n", fn);
    } else if (hdr.flags & CACHE_FLAG_IS_REDIRECT) {
        if (fseeko(fp, (off_t)hdr.header_size, SEEK_SET) == 0) {
            char *target_url
                = CALLOC((size_t)hdr.content_length + 1, sizeof(char));
            if (fread(target_url, 1, (size_t)hdr.content_length, fp)
                == (size_t)hdr.content_length) {
                fclose(fp);
                fp = NULL;
                FREE(full_path);
                FREE(fn);
                FREE(canon_url);
                res = CacheContainer_read_internal(
                    target_url, out_payload, out_payload_len, out_http_header,
                    out_http_header_len, out_cache_time, out_resolved_url,
                    depth + 1);
                FREE(target_url);
                return res;
            }
            FREE(target_url);
        }
    } else if (hdr.flags & CACHE_FLAG_IS_HEAD) {
        lprintf(debug,
                "cache container %s contains HEAD metadata only, payload not "
                "cached\n",
                fn);
        res = 0;
    } else if (hdr.content_length <= 0 || hdr.header_size < CACHE_PAGE_SIZE
               || hdr.header_size % CACHE_PAGE_SIZE != 0
               || hdr.url_len > PATH_MAX) {
        lprintf(error, "corrupt container geometry in %s\n", fn);
    } else if (stat(full_path, &cst) != 0
               || (uintmax_t)cst.st_size
                      < (uintmax_t)hdr.header_size
                            + (uintmax_t)hdr.content_length) {
        lprintf(error, "truncated container file %s\n", fn);
    } else {
        char *disk_url = CALLOC((size_t)hdr.url_len + 1, sizeof(char));
        int key_match = fread(disk_url, 1, (size_t)hdr.url_len + 1, fp)
                            == (size_t)hdr.url_len + 1
                        && !ferror(fp);
        if (key_match) {
            disk_url[hdr.url_len] = '\0';
            key_match = strcmp(key_url, disk_url) == 0;
        }
        if (!key_match) {
            lprintf(error, "cache key mismatch in %s\n", fn);
            FREE(disk_url);
        } else {
            if (out_resolved_url) {
                *out_resolved_url = STRDUP(disk_url);
            }
            FREE(disk_url);
            int64_t age = (int64_t)time(NULL) - hdr.cache_time;
            if (age > CONFIG.refresh_timeout) {
                lprintf(info,
                        "cache container %s expired (age: %jd, "
                        "limit: %d)\n",
                        fn, (intmax_t)age, CONFIG.refresh_timeout);
                res = 0;
            } else {
                if (hdr.http_header_len > 0) {
                    char *http_hdr
                        = CALLOC((size_t)hdr.http_header_len + 1, sizeof(char));
                    if (fread(http_hdr, 1, hdr.http_header_len, fp)
                            != hdr.http_header_len
                        || ferror(fp)) {
                        lprintf(error, "corrupt HTTP headers in %s\n", fn);
                        FREE(http_hdr);
                    } else {
                        *out_http_header = http_hdr;
                        *out_http_header_len = hdr.http_header_len;
                    }
                }
                if (hdr.http_header_len == 0 || *out_http_header != NULL) {
                    char *payload = CALLOC(1, (size_t)hdr.content_length + 1);
                    if (fseeko(fp, (off_t)hdr.header_size, SEEK_SET) != 0
                        || fread(payload, 1, (size_t)hdr.content_length, fp)
                               != (size_t)hdr.content_length
                        || ferror(fp)) {
                        lprintf(error, "corrupt payload in %s\n", fn);
                        FREE(payload);
                        FREE(*out_http_header);
                        *out_http_header = NULL;
                        *out_http_header_len = 0;
                    } else {
                        *out_payload = payload;
                        *out_payload_len = (size_t)hdr.content_length;
                        if (out_cache_time) {
                            *out_cache_time = (time_t)hdr.cache_time;
                        }
                        res = 1;
                    }
                }
            }
        }
    }

    if (fp && fclose(fp)) {
        lprintf(error, "fclose(%s): %s\n", fn, strerror(errno));
    }

    if (res == -1) {
        if (unlink(full_path) && errno != ENOENT) {
            lprintf(error, "unlink(): %s\n", strerror(errno));
        }
        FREE(*out_http_header);
        *out_http_header = NULL;
        *out_http_header_len = 0;
    }
    FREE(full_path);
    FREE(fn);
    FREE(canon_url);
    return res;
}

int CacheContainer_read_with_time(const char *url, char **out_payload,
                                  size_t *out_payload_len,
                                  char **out_http_header,
                                  size_t *out_http_header_len,
                                  time_t *out_cache_time,
                                  char **out_resolved_url)
{
    return CacheContainer_read_internal(url, out_payload, out_payload_len,
                                        out_http_header, out_http_header_len,
                                        out_cache_time, out_resolved_url, 0);
}

int CacheContainer_read(const char *url, char **out_payload,
                        size_t *out_payload_len, char **out_http_header,
                        size_t *out_http_header_len)
{
    return CacheContainer_read_with_time(url, out_payload, out_payload_len,
                                         out_http_header, out_http_header_len,
                                         NULL, NULL);
}

int CacheContainer_write_head(const char *url, long http_resp,
                              curl_off_t content_length, time_t remote_mtime,
                              const char *content_type, const char *raw_headers,
                              size_t raw_headers_len, LinkType link_type)
{
    if (!CACHE_SYSTEM_INIT || !url || !url[0]) {
        return -1;
    }

    char *canon_url = canonicalize_url(url);
    const char *key_url = canon_url ? canon_url : url;
    char *fn = string_to_cache_path(key_url);
    if (!fn) {
        FREE(canon_url);
        return -1;
    }

    char *full_path = path_append(CACHE_DIR, fn);
    ensure_parent_dir(full_path);

    char synth_header[256];
    const char *hdr_to_write = raw_headers;
    size_t hdr_len_to_write = raw_headers_len;
    if ((!hdr_to_write || hdr_len_to_write == 0) && content_type
        && content_type[0]) {
        snprintf(synth_header, sizeof(synth_header),
                 "HTTP/1.1 %ld OK\r\nContent-Type: %s\r\n\r\n",
                 http_resp > 0 ? http_resp : 200, content_type);
        hdr_to_write = synth_header;
        hdr_len_to_write = strlen(synth_header);
    }

    off_t header_size;
    off_t bitmap_offset;
    off_t http_header_offset;
    if (container_compute_layout(key_url, hdr_len_to_write, 0, &header_size,
                                 &bitmap_offset, &http_header_offset)) {
        FREE(full_path);
        FREE(fn);
        FREE(canon_url);
        return -1;
    }

    char tmp_path[PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", full_path, (int)getpid());
    int fd = open(tmp_path, O_RDWR | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
        FREE(full_path);
        FREE(fn);
        FREE(canon_url);
        return -1;
    }

    FILE *fp = fdopen(fd, "r+");
    if (!fp) {
        close(fd);
        unlink(tmp_path);
        FREE(full_path);
        FREE(fn);
        FREE(canon_url);
        return -1;
    }

    CacheHeader hdr;
    container_fill_header(&hdr, key_url, hdr_len_to_write, header_size,
                          (int64_t)remote_mtime, (off_t)content_length, 0, 0);
    hdr.flags
        = CACHE_FLAG_IS_HEAD | (link_type == LINK_DIR ? CACHE_FLAG_IS_DIR : 0);

    int ok = 1;
    if (fwrite(&hdr, 1, CACHE_HEADER_SIZE, fp) != CACHE_HEADER_SIZE) {
        ok = 0;
    }
    if (ok
        && fwrite(key_url, 1, (size_t)hdr.url_len + 1, fp)
               != (size_t)hdr.url_len + 1) {
        ok = 0;
    }
    if (ok && hdr_len_to_write > 0 && hdr_to_write
        && fwrite(hdr_to_write, 1, hdr_len_to_write, fp) != hdr_len_to_write) {
        ok = 0;
    }
    if (ok && fflush(fp) != 0) {
        ok = 0;
    }
    if (fclose(fp)) {
        ok = 0;
    }

    if (ok) {
        if (rename(tmp_path, full_path) != 0) {
            unlink(tmp_path);
            ok = 0;
        }
    } else {
        unlink(tmp_path);
    }

    FREE(full_path);
    FREE(fn);
    FREE(canon_url);
    return ok ? 0 : -1;
}

static int CacheContainer_read_head_internal(const char *url,
                                             CacheStat *stat_out, int depth)
{
    if (depth > 5 || !CACHE_SYSTEM_INIT || !url || !url[0]) {
        return 0;
    }

    char *canon_url = canonicalize_url(url);
    const char *key_url = canon_url ? canon_url : url;
    char *fn = string_to_cache_path(key_url);
    if (!fn) {
        FREE(canon_url);
        return 0;
    }

    char *full_path = path_append(CACHE_DIR, fn);
    FILE *fp = fopen(full_path, "r");
    if (!fp) {
        lprintf(debug, "cache head container not found for %s (%s)\n", url, fn);
        FREE(full_path);
        FREE(fn);
        FREE(canon_url);
        return 0;
    }

    int res = 0;
    CacheHeader hdr;
    if (fread(&hdr, 1, CACHE_HEADER_SIZE, fp) != CACHE_HEADER_SIZE
        || hdr.magic != CACHE_MAGIC || hdr.version != CACHE_VERSION) {
        res = -1;
    } else {
        int64_t age = (int64_t)time(NULL) - hdr.cache_time;
        if (age > CONFIG.refresh_timeout) {
            lprintf(
                info,
                "cache head container expired for %s (age: %jd, limit: %d)\n",
                url, (intmax_t)age, CONFIG.refresh_timeout);
            res = 0;
        } else if (hdr.flags & CACHE_FLAG_IS_REDIRECT) {
            if (fseeko(fp, (off_t)hdr.header_size, SEEK_SET) == 0) {
                char *target_url
                    = CALLOC((size_t)hdr.content_length + 1, sizeof(char));
                if (fread(target_url, 1, (size_t)hdr.content_length, fp)
                    == (size_t)hdr.content_length) {
                    fclose(fp);
                    fp = NULL;
                    res = CacheContainer_read_head_internal(
                        target_url, stat_out, depth + 1);
                }
                FREE(target_url);
            }
        } else if (hdr.flags
                   & (CACHE_FLAG_IS_HEAD | CACHE_FLAG_IS_COMPLETE
                      | CACHE_FLAG_IS_SPARSE)) {
            memset(stat_out, 0, sizeof(CacheStat));
            stat_out->content_length = (curl_off_t)hdr.content_length;
            stat_out->remote_mtime = (time_t)hdr.remote_mtime;
            stat_out->link_type
                = (hdr.flags & CACHE_FLAG_IS_DIR) ? LINK_DIR : LINK_FILE;
            stat_out->http_resp = 200;

            if (hdr.http_header_len > 0) {
                off_t hdr_off = (off_t)(CACHE_HEADER_SIZE + hdr.url_len + 1);
                if (fseeko(fp, hdr_off, SEEK_SET) == 0) {
                    char *raw
                        = CALLOC((size_t)hdr.http_header_len + 1, sizeof(char));
                    if (fread(raw, 1, hdr.http_header_len, fp)
                        == hdr.http_header_len) {
                        char *ct_line = strcasestr(raw, "Content-Type:");
                        if (ct_line) {
                            ct_line += 13;
                            while (*ct_line == ' ') {
                                ct_line++;
                            }
                            char *ct_end = strpbrk(ct_line, "\r\n;");
                            size_t ct_len = ct_end ? (size_t)(ct_end - ct_line)
                                                   : strlen(ct_line);
                            if (ct_len >= sizeof(stat_out->content_type)) {
                                ct_len = sizeof(stat_out->content_type) - 1;
                            }
                            strncpy(stat_out->content_type, ct_line, ct_len);
                            stat_out->content_type[ct_len] = '\0';
                        }
                    }
                    FREE(raw);
                }
            }
            res = 1;
        }
    }

    if (fp) {
        fclose(fp);
    }
    if (res == -1) {
        unlink(full_path);
    }
    FREE(full_path);
    FREE(fn);
    FREE(canon_url);
    return res;
}

int CacheContainer_read_head(const char *url, CacheStat *stat_out)
{
    return CacheContainer_read_head_internal(url, stat_out, 0);
}

int CacheContainer_write_redirect(const char *source_url,
                                  const char *target_url, long http_resp)
{
    if (!CACHE_SYSTEM_INIT || !source_url || !target_url) {
        return -1;
    }

    char *canon_src = canonicalize_url(source_url);
    char *canon_tgt = canonicalize_url(target_url);
    const char *key_src = canon_src ? canon_src : source_url;
    const char *key_tgt = canon_tgt ? canon_tgt : target_url;

    if (strcmp(key_src, key_tgt) == 0) {
        FREE(canon_src);
        FREE(canon_tgt);
        return 0;
    }

    char *fn = string_to_cache_path(key_src);
    if (!fn) {
        FREE(canon_src);
        FREE(canon_tgt);
        return -1;
    }

    char *full_path = path_append(CACHE_DIR, fn);
    ensure_parent_dir(full_path);

    char tmp_path[PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", full_path, (int)getpid());
    int fd = open(tmp_path, O_RDWR | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
        FREE(full_path);
        FREE(fn);
        FREE(canon_src);
        FREE(canon_tgt);
        return -1;
    }

    FILE *fp = fdopen(fd, "r+");
    if (!fp) {
        close(fd);
        unlink(tmp_path);
        FREE(full_path);
        FREE(fn);
        FREE(canon_src);
        FREE(canon_tgt);
        return -1;
    }

    size_t target_len = strlen(key_tgt);
    off_t header_size;
    off_t bitmap_offset;
    off_t http_header_offset;
    if (container_compute_layout(key_src, 0, 0, &header_size, &bitmap_offset,
                                 &http_header_offset)) {
        fclose(fp);
        unlink(tmp_path);
        FREE(full_path);
        FREE(fn);
        FREE(canon_src);
        FREE(canon_tgt);
        return -1;
    }

    CacheHeader hdr;
    container_fill_header(&hdr, key_src, 0, header_size, 0, (off_t)target_len,
                          (int32_t)http_resp, 0);
    hdr.flags = CACHE_FLAG_IS_REDIRECT;

    int ok = 1;
    if (fwrite(&hdr, 1, CACHE_HEADER_SIZE, fp) != CACHE_HEADER_SIZE) {
        ok = 0;
    }
    if (ok
        && fwrite(key_src, 1, (size_t)hdr.url_len + 1, fp)
               != (size_t)hdr.url_len + 1) {
        ok = 0;
    }
    if (ok && fseeko(fp, header_size - 1, SEEK_SET) != 0) {
        ok = 0;
    }
    if (ok && fputc('\0', fp) != '\0') {
        ok = 0;
    }
    if (ok && fwrite(key_tgt, 1, target_len, fp) != target_len) {
        ok = 0;
    }
    if (ok && fflush(fp) != 0) {
        ok = 0;
    }
    if (fclose(fp)) {
        ok = 0;
    }

    if (ok) {
        if (rename(tmp_path, full_path) != 0) {
            unlink(tmp_path);
            ok = 0;
        }
    } else {
        unlink(tmp_path);
    }

    FREE(full_path);
    FREE(fn);
    FREE(canon_src);
    FREE(canon_tgt);
    return ok ? 0 : -1;
}
