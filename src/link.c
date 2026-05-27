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
 * \file link.c
 * \brief Link structure and handling functions implementation
 */

#include "link.h"

#include "cache.h"
#include "config.h"
#include "log.h"
#include "memcache.h"
#include "network.h"
#include "util.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <gumbo.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>
#include <unistd.h>

#define STATUS_LEN 64

/*
 * ---------------- External variables -----------------------
 */
LinkTable *ROOT_LINK_TBL = NULL;
int ROOT_LINK_OFFSET = 0;

/**
 * \brief LinkTable generation priority lock
 * \details This allows LinkTable generation to be run exclusively. This
 * effectively gives LinkTable generation priority over file transfer.
 */
static pthread_mutex_t link_lock = PTHREAD_MUTEX_INITIALIZER;
static void make_link_relative(const char *page_url, char *link_url);

/**
 * \brief create a new Link
 */
static Link *Link_new(const char *linkname, LinkType type)
{
    Link *link = CALLOC(1, sizeof(Link));

    strncpy(link->linkname, linkname, NAME_MAX);
    strncpy(link->linkpath, linkname, NAME_MAX);
    link->type = type;

    /*
     * remove the '/' from linkname if it exists
     */
    size_t len = strnlen(link->linkname, NAME_MAX);
    if (len > 0) {
        char *c = &(link->linkname[len - 1]);
        if (*c == '/') {
            *c = '\0';
        }
    }

    return link;
}

static int is_same_origin(const char *link_url)
{
    return !CONFIG.external_links || !ROOT_LINK_TBL
           || !is_cross_origin(ROOT_LINK_TBL->links[0]->f_url, link_url);
}

static CURL *Link_to_curl(Link *link)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        lprintf(fatal, "curl_easy_init() failed!\n");
    }
    /*
     * set up some basic curl stuff
     */
    CURLcode ret = curl_easy_setopt(curl, CURLOPT_USERAGENT, CONFIG.user_agent);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    /*
     * for following directories without the '/'
     */
    ret = curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 2);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_URL, link->f_url);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_PIPEWAIT, 1L);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    if (curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_3)) {
        ret = curl_easy_setopt(curl, CURLOPT_HTTP_VERSION,
                               CURL_HTTP_VERSION_2_0);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    }
    ret = curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_SHARE, CURL_SHARE);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    if (CONFIG.cafile || CONFIG.capath) {
        /*
         * Having been given a certificate file or directory, disable any search
         * paths built into libcurl, so that we exclusively use the explicitly
         * given certificate(s).
         */
        ret = curl_easy_setopt(curl, CURLOPT_CAPATH, CONFIG.capath);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }

        ret = curl_easy_setopt(curl, CURLOPT_CAINFO, CONFIG.cafile);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.insecure_tls) {
        ret = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.log_type & libcurl_debug) {
        ret = curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.http_headers) {
        if (is_same_origin(link->f_url)) {
            ret = curl_easy_setopt(curl, CURLOPT_HTTPHEADER,
                                   CONFIG.http_headers);
            if (ret) {
                lprintf(error, "%s\n", curl_easy_strerror(ret));
            }
        }
    }

    if (CONFIG.http_username) {
        /*
         * Only apply credentials to the mounted server. When
         * --external-links is active, cross-origin links must NOT receive
         * the user's credentials for the primary server.
         */
        if (is_same_origin(link->f_url)) {
            ret = curl_easy_setopt(curl, CURLOPT_USERNAME,
                                   CONFIG.http_username);
            if (ret) {
                lprintf(error, "%s\n", curl_easy_strerror(ret));
            }
        }
    }

    if (CONFIG.http_password) {
        if (is_same_origin(link->f_url)) {
            ret = curl_easy_setopt(curl, CURLOPT_PASSWORD,
                                   CONFIG.http_password);
            if (ret) {
                lprintf(error, "%s\n", curl_easy_strerror(ret));
            }
        }
    }

    if (CONFIG.proxy) {
        ret = curl_easy_setopt(curl, CURLOPT_PROXY, CONFIG.proxy);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.proxy_username) {
        ret = curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME,
                               CONFIG.proxy_username);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.proxy_password) {
        ret = curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD,
                               CONFIG.proxy_password);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.proxy_cafile || CONFIG.proxy_capath) {
        /* See CONFIG.cafile above */
        ret = curl_easy_setopt(curl, CURLOPT_PROXY_CAPATH, CONFIG.proxy_capath);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }

        ret = curl_easy_setopt(curl, CURLOPT_PROXY_CAINFO, CONFIG.proxy_cafile);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    }

    return curl;
}

static void Link_req_file_stat(Link *this_link)
{
    CURL *curl = Link_to_curl(this_link);
    CURLcode ret = curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }

    /*
     * We need to put the variable on the heap, because otherwise the
     * variable gets popped from the stack as the function returns.
     *
     * It gets freed in curl_process_msgs();
     */
    TransferStruct *transfer = CALLOC(1, sizeof(TransferStruct));

    transfer->link = this_link;
    transfer->type = FILESTAT;
    ret = curl_easy_setopt(curl, CURLOPT_PRIVATE, transfer);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }

    transfer_nonblocking(curl);
}

/**
 * \brief Fill in the uninitialised entries in a link table
 * \details Try and get the stats for each link in the link table. This will get
 * repeated until the uninitialised entry count drop to zero.
 */
static void LinkTable_uninitialised_fill(LinkTable *linktbl)
{
    if (!linktbl) {
        return;
    }
    int u;
    char s[STATUS_LEN];

    /*
     * Start all uninitialized requests once
     */
    int total_uninitialized = 0;
    for (int i = 0; i < linktbl->size; i++) {
        Link *this_link = linktbl->links[i];
        if (this_link->type == LINK_UNINITIALISED_FILE
            || this_link->type == LINK_UNINITIALISED_DIR) {
            Link_req_file_stat(linktbl->links[i]);
            total_uninitialized++;
        }
    }

    if (total_uninitialized == 0) {
        return;
    }

    int n = total_uninitialized;
    int j = 0;
    do {
        u = 0;
        for (int i = 0; i < linktbl->size; i++) {
            Link *this_link = linktbl->links[i];
            if (this_link->type == LINK_UNINITIALISED_FILE
                || this_link->type == LINK_UNINITIALISED_DIR) {
                u++;
            }
        }

        if (u > 0) {
            if (CONFIG.log_type & debug) {
                if (j) {
                    erase_string(stderr, STATUS_LEN, s);
                }
                snprintf(s, STATUS_LEN, "%d / %d", n - u, n);
                fprintf(stderr, "%s", s);
                j++;
            }

            /*
             * Block until some handles are processed
             */
            int n_running = curl_multi_perform_once();

            if (n_running == 0) {
                for (int i = 0; i < linktbl->size; i++) {
                    Link *this_link = linktbl->links[i];
                    if (this_link->type == LINK_UNINITIALISED_FILE
                        || this_link->type == LINK_UNINITIALISED_DIR) {
                        lprintf(error, "Failed to initialize: %s\n",
                                this_link->f_url);
                        this_link->type = LINK_INVALID;
                    }
                }
                break;
            }
        }
    } while (u > 0);

    if (CONFIG.log_type & debug) {
        erase_string(stderr, STATUS_LEN, s);
        fprintf(stderr, "... Done!\n");
    }
}

/**
 * \brief Create the root linktable for single file mode
 */
static LinkTable *single_LinkTable_new(const char *url)
{
    const char *orig_ptr = strrchr(url, '/') + 1;
    char *ptr = curl_easy_unescape(NULL, orig_ptr, 0, NULL);
    LinkTable *linktbl = LinkTable_alloc(url);
    Link *link = Link_new(ptr ? ptr : orig_ptr, LINK_UNINITIALISED_FILE);
    strncpy(link->f_url, url, PATH_MAX);
    if (ptr) {
        curl_free(ptr);
    }
    LinkTable_add(linktbl, link);
    LinkTable_uninitialised_fill(linktbl);
    LinkTable_print(linktbl);
    return linktbl;
}

LinkTable *LinkSystem_init(const char *url)
{
    size_t len = strnlen(url, PATH_MAX);
    /*
     * --------- Set the length of the root link -----------
     */
    /*
     * This is where the '/' should be
     */
    ROOT_LINK_OFFSET
        = (len > 0 && url[len - 1] == '/') ? (int)len - 1 : (int)len;

    /*
     * --------------------- Enable cache system --------------------
     */
    if (CONFIG.cache_enabled) {
        if (CONFIG.cache_dir) {
            CacheSystem_init(CONFIG.cache_dir, 0);
        } else {
            CacheSystem_init(url, 1);
        }
    }

    /*
     * ----------- Create the root link table --------------
     */
    if (CONFIG.mode == NORMAL) {
        ROOT_LINK_TBL = LinkTable_new(url);
    } else if (CONFIG.mode == SINGLE) {
        ROOT_LINK_TBL = single_LinkTable_new(url);
    } else if (CONFIG.mode == SONIC) {
        sonic_config_init(url, CONFIG.sonic_username, CONFIG.sonic_password);
        if (!CONFIG.sonic_id3) {
            ROOT_LINK_TBL = sonic_LinkTable_new_index("0");
        } else {
            ROOT_LINK_TBL = sonic_LinkTable_new_id3(0, "0");
        }
    } else {
        lprintf(fatal, "Invalid CONFIG.mode\n");
    }
    return ROOT_LINK_TBL;
}

void LinkTable_add(LinkTable *linktbl, Link *link)
{
    linktbl->links = (Link **)REALLOC(
        (void *)linktbl->links, ((size_t)linktbl->size + 1) * sizeof(Link *));
    linktbl->links[linktbl->size] = link;
    link->parent_table = linktbl;
    linktbl->size++;
}

static LinkType linkname_to_LinkType(const char *linkname)
{
    if (linkname[0] == '\0' || linkname[0] == '/') {
        return LINK_INVALID;
    }

    /* Now allow all printable characters */
    for (int i = 0; linkname[i] != '\0'; i++) {
        char c = linkname[i];
        if (!isprint(c)) {
            return LINK_INVALID;
        }
    }

    /* The linkname must not contain '/' in the middle. */
    const char *slash = strchr(linkname, '/');
    if (slash) {
        int linkname_len = strnlen(linkname, NAME_MAX) - 1;
        if (slash - linkname != linkname_len) {
            return LINK_INVALID;
        }
    }

    /* '/' must be at the end to be a valid directory name */
    if (linkname[strnlen(linkname, NAME_MAX) - 1] == '/') {
        return LINK_UNINITIALISED_DIR;
    }

    return LINK_UNINITIALISED_FILE;
}

int link_linknames_equal(const char *str_a, const char *str_b)
{
    if (!str_a || !str_b) {
        return 0;
    }
    size_t len_a = strnlen(str_a, NAME_MAX);
    size_t len_b = strnlen(str_b, NAME_MAX);
    size_t max_len = MAX(len_a, len_b);
    size_t comp_len = MIN(len_a, len_b);
    int identical = 0;

    /* The length of the strings differ by more than 1 character. */
    if (max_len - comp_len > 1) {
        goto end;
    }

    /* Assuming that the shorter string has a non-zero length */
    if (comp_len) {
        /* Assuming that the common parts of the strings are the same */
        if (!strncmp(str_a, str_b, comp_len)) {
            /* If the lengths are equal, they are identical */
            if (len_a == len_b) {
                identical = 1;
            } else {
                /* Otherwise the last character of the longer string should be
                 * '/' */
                const char *longer_str = len_a > len_b ? str_a : str_b;
                identical = (longer_str[comp_len] == '/');
            }
        }
    }

end:
    return identical;
}

struct LinkHashSet {
    const char **buckets;
    int capacity;
    int size;
};

unsigned int link_hash_str(const char *str)
{
    unsigned int hash = 5381;
    int c;
    size_t len = strnlen(str, NAME_MAX);

    /* Strip all trailing slashes */
    while (len > 0 && str[len - 1] == '/') {
        len--;
    }

    for (size_t i = 0; i < len; i++) {
        c = (unsigned char)str[i];
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

LinkHashSet *LinkHashSet_new(int capacity)
{
    if (capacity <= 0) {
        capacity = 16;
    } else {
        int power = 1;
        while (power < capacity && power < (1 << 30)) {
            power <<= 1;
        }
        capacity = power;
    }
    LinkHashSet *set = (LinkHashSet *)CALLOC(1, sizeof(LinkHashSet));
    set->capacity = capacity;
    set->buckets = (const char **)CALLOC(capacity, sizeof(const char *));
    return set;
}

static void LinkHashSet_resize(LinkHashSet *set)
{
    if (set->capacity <= 0) {
        return;
    }
    int old_capacity = set->capacity;
    const char **old_buckets = set->buckets;
    if (set->capacity > INT_MAX / 2) {
        lprintf(fatal, "LinkHashSet capacity overflow\n");
    }
    set->capacity *= 2;
    set->buckets = (const char **)CALLOC(set->capacity, sizeof(const char *));

    for (int i = 0; i < old_capacity; i++) {
        if (old_buckets[i]) {
            unsigned int hash = link_hash_str(old_buckets[i]);
            int bucket = hash & (set->capacity - 1);
            while (set->buckets[bucket] != NULL) {
                bucket = (bucket + 1) & (set->capacity - 1);
            }
            set->buckets[bucket] = old_buckets[i];
        }
    }
    FREE(old_buckets);
}

int LinkHashSet_add(LinkHashSet *set, const char *linkname)
{
    if (!set || !linkname || set->capacity <= 0) {
        return 0;
    }
    if (set->size >= set->capacity / 2) {
        LinkHashSet_resize(set);
    }
    unsigned int hash = link_hash_str(linkname);
    int bucket = hash & (set->capacity - 1);
    while (set->buckets[bucket] != NULL) {
        if (link_linknames_equal(set->buckets[bucket], linkname)) {
            return 0;
        }
        bucket = (bucket + 1) & (set->capacity - 1);
    }
    set->buckets[bucket] = STRDUP(linkname);
    set->size++;
    return 1;
}

void LinkHashSet_free(LinkHashSet *set)
{
    if (!set) {
        return;
    }
    for (int i = 0; i < set->capacity; i++) {
        if (set->buckets[i]) {
            char *ptr = (char *)set->buckets[i];
            FREE(ptr);
        }
    }
    FREE(set->buckets);
    FREE(set);
}

/**
 * \brief Check if a URL is an external (absolute) URL.
 * \return 1 if the URL starts with http:// or https://, 0 otherwise
 */
int is_external_url(const char *url)
{
    if (!url) {
        return 0;
    }
    return (strncasecmp(url, "http://", 7) == 0
            || strncasecmp(url, "https://", 8) == 0);
}

/**
 * \brief Check if link_url has a different origin than page_url.
 * \details Compares scheme + host + port by finding the path component
 *          (the third '/') of each URL and doing a prefix comparison.
 * \return 1 if cross-origin, 0 if same origin
 */
int is_cross_origin(const char *page_url, const char *link_url)
{
    if (!page_url || !link_url) {
        return 1;
    }
    /*
     * Walk past "scheme://host:port" in both URLs and compare the
     * prefix up to (but not including) the first path slash.
     * If either URL has fewer than 3 slashes, check if it is a valid
     * absolute URL to determine the origin length.
     */
    int slashes = 0;
    const char *p = page_url;
    while (*p && slashes < 3) {
        if (*p == '/') {
            slashes++;
        }
        p++;
    }
    size_t page_origin_len;
    if (slashes < 3) {
        if (is_external_url(page_url)) {
            page_origin_len = strlen(page_url);
        } else {
            return 1; /* malformed page_url */
        }
    } else {
        page_origin_len = (size_t)(p - page_url) - 1;
    }

    slashes = 0;
    const char *l = link_url;
    while (*l && slashes < 3) {
        if (*l == '/') {
            slashes++;
        }
        l++;
    }
    size_t link_origin_len;
    if (slashes < 3) {
        if (is_external_url(link_url)) {
            link_origin_len = strlen(link_url);
        } else {
            return 1; /* malformed link_url */
        }
    } else {
        link_origin_len = (size_t)(l - link_url) - 1;
    }

    if (page_origin_len != link_origin_len) {
        return 1;
    }
    return strncasecmp(page_url, link_url, page_origin_len) != 0;
}

/**
 * \brief Extract the filename component from an external URL.
 * \details For "http://example.com/path/file.iso" returns "file.iso".
 *          For "http://example.com/path/dir/" returns "dir".
 *          Query strings ("?") are stripped.
 *          Returns empty string for a root-only URL.
 * \note The caller must free the returned string with FREE().
 */
char *external_url_to_filename(const char *url)
{
    if (!url) {
        return STRDUP("");
    }
    /*
     * Skip the scheme://host:port/ prefix — walk past the third slash.
     */
    int slashes = 0;
    const char *p = url;
    while (*p && slashes < 3) {
        if (*p == '/') {
            slashes++;
        }
        p++;
    }
    /* p now points to the first character of the path (after the
     * trailing slash of the origin, e.g. "path/file.iso"). */

    if (*p == '\0') {
        /* Root-only URL — no filename to extract. */
        return STRDUP("");
    }

    size_t path_len = strlen(p);
    char *path_copy = STRNDUP(p, path_len);

    /* Strip query string and fragment identifier if present (must be done
     * before trailing slash check).
     */
    char *q = strpbrk(path_copy, "?#");
    if (q) {
        *q = '\0';
    }

    /* Strip trailing slash if present. */
    path_len = strlen(path_copy);
    if (path_len > 0 && path_copy[path_len - 1] == '/') {
        path_copy[path_len - 1] = '\0';
    }

    /* Find the last '/' and take everything after it. */
    char *last_slash = strrchr(path_copy, '/');
    char *filename;
    if (last_slash) {
        filename = STRDUP(last_slash + 1);
    } else {
        filename = STRDUP(path_copy);
    }
    FREE(path_copy);

    /* URL-decode the filename so it can be used as a filesystem name. */
    char *decoded = curl_easy_unescape(NULL, filename, 0, NULL);
    if (!decoded) {
        return filename;
    }
    char *result = STRDUP(decoded);
    curl_free(decoded);
    FREE(filename);
    return result;
}

/**
 * Shamelessly copied and pasted from:
 * https://github.com/google/gumbo-parser/blob/master/examples/find_links.cc
 */
static void HTML_to_LinkTable(const char *url, GumboNode *node,
                              LinkTable *linktbl, LinkHashSet *set)
{
    if (node->type != GUMBO_NODE_ELEMENT) {
        return;
    }
    GumboAttribute *href;
    href = gumbo_get_attribute(&node->v.element.attributes, "href");
    if (node->v.element.tag == GUMBO_TAG_A && href) {
        const char *raw_href = href->value;

        if (CONFIG.external_links && is_external_url(raw_href)
            && is_cross_origin(url, raw_href)) {
            /*
             * -------- External (cross-origin) link handling --------
             * Extract the filename from the external URL and create a
             * Link with f_url already pointing to the external server.
             * LinkTable_fill() will skip URL-construction for these.
             */
            char *filename = external_url_to_filename(raw_href);
            if (filename && filename[0] != '\0' && strcmp(filename, ".") != 0
                && strcmp(filename, "..") != 0) {
                /* Determine type: directory if URL (ignoring query/fragment)
                 * ends with '/' */
                const char *qf = strpbrk(raw_href, "?#");
                size_t href_len
                    = qf ? (size_t)(qf - raw_href) : strlen(raw_href);
                LinkType type = (href_len > 0 && raw_href[href_len - 1] == '/')
                                    ? LINK_UNINITIALISED_DIR
                                    : LINK_UNINITIALISED_FILE;

                /* First-wins: skip if a link with this name already exists */
                if (LinkHashSet_add(set, filename)) {
                    Link *link = Link_new(filename, type);
                    strncpy(link->f_url, raw_href, PATH_MAX);
                    LinkTable_add(linktbl, link);
                }
            }
            FREE(filename);
        } else {
            /*
             * -------- Same-origin / relative link handling (unchanged)
             * --------
             */
            char *relative_url = STRNDUP(raw_href, PATH_MAX);
            make_link_relative(url, relative_url);

            /* Truncate at the first slash to support links to subdirectories */
            char *slash = strchr(relative_url, '/');
            if (slash && slash != relative_url) {
                /* Don't truncate full URIs like http://... */
                if (*(slash - 1) != ':' && slash[1] != '/') {
                    slash[1] = '\0';
                }
            }

            /* if it is valid, copy the link onto the heap */
            LinkType type = linkname_to_LinkType(relative_url);

            /* Check if the new link is a duplicate */
            if ((type == LINK_UNINITIALISED_DIR)
                || (type == LINK_UNINITIALISED_FILE)) {
                if (LinkHashSet_add(set, relative_url)) {
                    LinkTable_add(linktbl, Link_new(relative_url, type));
                }
            }
            FREE(relative_url);
        }
    }

    /* Note the recursive call */
    GumboVector *children = &node->v.element.children;
    for (size_t i = 0; i < children->length; ++i) {
        HTML_to_LinkTable(url, (GumboNode *)children->data[i], linktbl, set);
    }
}

void LinkTable_parse_html(LinkTable *linktbl, const char *url, const char *html)
{
    if (!linktbl || !url || !html) {
        return;
    }
    GumboOutput *output = gumbo_parse(html);
    if (!output) {
        return;
    }
    LinkHashSet *set = LinkHashSet_new(4096);
    if (output->root) {
        HTML_to_LinkTable(url, output->root, linktbl, set);
    }
    LinkHashSet_free(set);
    gumbo_destroy_output(&kGumboDefaultOptions, output);
}
void Link_set_file_stat(Link *this_link, CURL *curl)
{
    long http_resp;
    CURLcode ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    if (http_resp == HTTP_OK) {
        curl_off_t cl = 0;
        ret = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
        ret = curl_easy_getinfo(curl, CURLINFO_FILETIME, &(this_link->time));
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }

        if (this_link->type == LINK_UNINITIALISED_FILE) {
            if (cl < 0) {
                this_link->type = LINK_INVALID;
            } else if (cl == 0 && CONFIG.zero_len_is_dir) {
                this_link->type = LINK_DIR;
            } else {
                this_link->type = LINK_FILE;
                this_link->content_length = cl;
            }
        } else if (this_link->type == LINK_UNINITIALISED_DIR) {
            this_link->type = LINK_DIR;
        }

    } else {
        lprintf(warning, "%s: HTTP %ld\n", this_link->f_url, http_resp);
        /*
         * Emit a targeted warning if an external link needs authentication
         * that we are not providing.
         */
        if (CONFIG.external_links && (http_resp == 401 || http_resp == 403)
            && ROOT_LINK_TBL
            && is_cross_origin(ROOT_LINK_TBL->links[0]->f_url,
                               this_link->f_url)) {
            lprintf(warning,
                    "External link %s requires authentication (HTTP %ld). "
                    "Credentials are only applied to the mounted "
                    "server.\n",
                    this_link->f_url, http_resp);
        }
        if (HTTP_temp_failure(http_resp) || CONFIG.invalid_refresh) {
            lprintf(warning, ", retrying later.\n");
        } else {
            this_link->type = LINK_INVALID;
        }
    }
}

static void LinkTable_fill(LinkTable *linktbl)
{
    Link *head_link = linktbl->links[0];
    for (int i = 1; i < linktbl->size; i++) {
        Link *this_link = linktbl->links[i];

        /*
         * External links have f_url pre-populated by HTML_to_LinkTable().
         * Skip URL construction for them.
         */
        if (this_link->f_url[0] != '\0') {
            continue;
        }

        /* Some web sites use characters in their href attributes that really
           shouldn't be in their href attributes, most commonly spaces. And
           some web sites _do_ properly encode their href attributes. So we
           first unescape the link path, and then we escape it, so that curl
           will definitely be happy with it (e.g., curl won't accept URLs with
           spaces in them!). If we only escaped it, and there were already
           encoded characters in it, then that would break the link. */
        char *unescaped_path
            = curl_easy_unescape(NULL, this_link->linkpath, 0, NULL);
        char *escaped_path = curl_easy_escape(
            NULL, unescaped_path ? unescaped_path : this_link->linkpath, 0);
        if (unescaped_path) {
            curl_free(unescaped_path);
        }

        if (escaped_path) {
            /* Our code does the wrong thing if there's a trailing slash that's
               been replaced with %2F, which curl_easy_escape does, God bless
               it, so if it did that then let's put it back. */
            int escaped_len = strlen(escaped_path);
            if (escaped_len >= 3
                && !strcmp(escaped_path + escaped_len - 3, "%2F")) {
                escaped_path[escaped_len - 3] = '/';
                escaped_path[escaped_len - 2] = '\0';
            }
            char *url = path_append(head_link->f_url, escaped_path);
            curl_free(escaped_path);
            strncpy(this_link->f_url, url, PATH_MAX);
            FREE(url);
        } else {
            /* Fallback in case escape fails */
            char *url = path_append(head_link->f_url, this_link->linkpath);
            strncpy(this_link->f_url, url, PATH_MAX);
            FREE(url);
        }

        char *unescaped_linkname
            = curl_easy_unescape(NULL, this_link->linkname, 0, NULL);
        if (unescaped_linkname) {
            snprintf(this_link->linkname, sizeof(this_link->linkname), "%s",
                     unescaped_linkname);
            curl_free(unescaped_linkname);
        }
    }
    LinkTable_uninitialised_fill(linktbl);
}

void LinkTable_ref(LinkTable *tbl)
{
    if (!tbl) {
        return;
    }
    PTHREAD_MUTEX_LOCK(&link_lock);
    tbl->refcount++;
    tbl->orphaned = 0;
    PTHREAD_MUTEX_UNLOCK(&link_lock);
}

void LinkTable_mark_orphaned(LinkTable *tbl)
{
    if (!tbl) {
        return;
    }
    PTHREAD_MUTEX_LOCK(&link_lock);
    tbl->orphaned = 1;
    PTHREAD_MUTEX_UNLOCK(&link_lock);
}

void LinkTable_unref(LinkTable *tbl)
{
    if (!tbl) {
        return;
    }
    PTHREAD_MUTEX_LOCK(&link_lock);
    tbl->refcount--;
    if (tbl->refcount == 0 && tbl->orphaned) {
        LinkTable *parent = tbl->parent_tbl;
        Link *parent_link = tbl->parent_link;
        if (parent_link) {
            parent_link->next_table = NULL;
        }
        PTHREAD_MUTEX_UNLOCK(&link_lock);

        LinkTable_free(tbl);

        if (parent) {
            LinkTable_unref(parent);
        }
        return;
    }
    PTHREAD_MUTEX_UNLOCK(&link_lock);
}

void LinkTable_free(LinkTable *linktbl)
{
    if (linktbl) {
        for (int i = 0; i < linktbl->size; i++) {
            Link *entry = linktbl->links ? linktbl->links[i] : NULL;
            if (!entry) {
                continue;
            }
            LinkTable_free(entry->next_table);
            FREE(entry);
        }
        FREE(linktbl->links);
        FREE(linktbl);
    }
}

void LinkTable_print(LinkTable *linktbl)
{
    if (CONFIG.log_type & info) {
        int j = 0;
        lprintf(info, "--------------------------------------------\n");
        lprintf(info, " LinkTable %p for %s\n", (void *)linktbl,
                linktbl->links[0]->f_url);
        lprintf(info, "--------------------------------------------\n");
        for (int i = 0; i < linktbl->size; i++) {
            Link *this_link = linktbl->links[i];
            lprintf(info, "%d %c %lu %s %s\n", i, this_link->type,
                    this_link->content_length, this_link->linkname,
                    this_link->f_url);
            if ((this_link->type != LINK_FILE) && (this_link->type != LINK_DIR)
                && (this_link->type != LINK_HEAD)) {
                j++;
            }
        }
        lprintf(info, "--------------------------------------------\n");
        lprintf(info, " Invalid link count: %d\n", j);
        lprintf(info, "--------------------------------------------\n");
    }
}

LinkTable *LinkTable_alloc(const char *url)
{
    LinkTable *linktbl = CALLOC(1, sizeof(LinkTable));
    linktbl->size = 0;
    linktbl->index_time = 0;
    linktbl->links = NULL;


    /*
     * populate the base URL
     */
    Link *head_link = Link_new("/", LINK_HEAD);
    LinkTable_add(linktbl, head_link);
    strncpy(head_link->f_url, url, PATH_MAX);
    assert(linktbl->size == 1);
    return linktbl;
}

char *url_to_cache_path(const char *url)
{
    if (!url) {
        return NULL;
    }
    char *unescaped_path;
    /*
     * When --external-links is active a directory link from an external
     * server may be navigated. Its URL won't share the root server's
     * origin, so applying ROOT_LINK_OFFSET would produce garbage. Detect
     * this case by checking whether url is cross-origin from root.
     */
    if (ROOT_LINK_TBL && is_cross_origin(ROOT_LINK_TBL->links[0]->f_url, url)) {
        /* External URL: use the full URL as the cache key path. */
        char *temp = curl_easy_unescape(NULL, url, 0, NULL);
        unescaped_path = temp ? STRDUP(temp) : STRDUP(url);
        if (temp) {
            curl_free(temp);
        }
        /* Sanitize unescaped_path to prevent path traversal via ".." */
        char *p = unescaped_path;
        while ((p = strstr(p, ".."))) {
            p[0] = '_';
            p[1] = '_';
            p += 2;
        }
        /* Sanitize unescaped_path to prevent path traversal and invalid
         * directory structures */
        for (char *sp = unescaped_path; *sp; sp++) {
            if (*sp == '/' || *sp == ':') {
                *sp = '_';
            }
        }
    } else {
        size_t url_len = strlen(url);
        const char *offset_url = (url_len >= (size_t)ROOT_LINK_OFFSET)
                                     ? url + ROOT_LINK_OFFSET
                                     : url;
        char *temp = curl_easy_unescape(NULL, offset_url, 0, NULL);
        unescaped_path = temp ? STRDUP(temp) : STRDUP(offset_url);
        if (temp) {
            curl_free(temp);
        }
    }
    return unescaped_path;
}

LinkTable *LinkTable_new(const char *url)
{
    char *unescaped_path = url_to_cache_path(url);
    LinkTable *linktbl = NULL;

    /*
     * Attempt to load the LinkTable from the disk.
     */
    if (CACHE_SYSTEM_INIT) {
        CacheDir_create(unescaped_path);
        LinkTable *disk_linktbl;

        disk_linktbl = LinkTable_disk_open(unescaped_path);
        if (disk_linktbl) {
            /*
             * Check if the LinkTable needs to be refreshed based on timeout.
             */
            time_t time_now = time(NULL);
            if (time_now - disk_linktbl->index_time > CONFIG.refresh_timeout) {
                lprintf(info, "time_now: %ld, index_time: %ld\n",
                        (long)time_now, (long)disk_linktbl->index_time);
                lprintf(info, "diff: %ld, limit: %d\n",
                        (long)(time_now - disk_linktbl->index_time),
                        CONFIG.refresh_timeout);
                LinkTable_free(disk_linktbl);
            } else {
                linktbl = disk_linktbl;
            }
        }
    }

    /*
     * Download a new LinkTable because we didn't manage to load it from the
     * disk
     */
    if (!linktbl) {
        linktbl = LinkTable_alloc(url);
        linktbl->index_time = time(NULL);

        /*
         * start downloading the base URL
         */
        TransferStruct ts = Link_download_full(linktbl->links[0]);
        if (ts.curr_size == 0) {
            LinkTable_free(linktbl);
            return NULL;
        }

        /*
         * Otherwise parsed the received data
         */
        LinkTable_parse_html(linktbl, url, ts.data);
        FREE(ts.data);


        LinkTable_fill(linktbl);

        /*
         * Save the link table
         */
        if (CACHE_SYSTEM_INIT && LinkTable_disk_save(linktbl, unescaped_path)) {
            lprintf(error, "Failed to save the LinkTable!\n");
        }
    }

    FREE(unescaped_path);
    LinkTable_print(linktbl);
    return linktbl;
}

static void LinkTable_disk_delete(const char *dirn)
{
    char *metadirn = path_append(META_DIR, dirn);
    char *path = path_append(metadirn, ".LinkTable");
    if (unlink(path)) {
        lprintf(error, "unlink(%s): %s\n", path, strerror(errno));
    }
    FREE(path);
    FREE(metadirn);
}

/* This is necessary to get the compiler on some platforms to stop
   complaining about the fact that we're not using the return value of
   fread, when we know we aren't and that's fine. */
static inline void ignore_value(int i)
{
    (void)i;
}

int LinkTable_disk_save(LinkTable *linktbl, const char *dirn)
{
    char *metadirn = path_append(META_DIR, dirn);
    char *path = path_append(metadirn, ".LinkTable");
    FILE *fp = fopen(path, "w");
    FREE(metadirn);

    if (!fp) {
        lprintf(error, "fopen(%s): %s\n", path, strerror(errno));
        FREE(path);
        return -1;
    }

    if (fwrite(&linktbl->size, sizeof(int), 1, fp) != 1
        || fwrite(&linktbl->index_time, sizeof(time_t), 1, fp) != 1) {
        lprintf(error, "Failed to save the header of %s!\n", path);
    }
    FREE(path);
    for (int i = 0; i < linktbl->size; i++) {
        ignore_value(
            fwrite(linktbl->links[i]->linkname, sizeof(char), NAME_MAX, fp));
        ignore_value(
            fwrite(linktbl->links[i]->f_url, sizeof(char), PATH_MAX, fp));
        ignore_value(fwrite(&linktbl->links[i]->type, sizeof(LinkType), 1, fp));
        ignore_value(
            fwrite(&linktbl->links[i]->content_length, sizeof(size_t), 1, fp));
        ignore_value(fwrite(&linktbl->links[i]->time, sizeof(long), 1, fp));
    }

    int res = 0;

    if (ferror(fp)) {
        lprintf(error, "encountered ferror!\n");
        res = -1;
    }

    if (fclose(fp)) {
        lprintf(error, "cannot close the file pointer, %s\n", strerror(errno));
        res = -1;
    }

    return res;
}

LinkTable *LinkTable_disk_open(const char *dirn)
{
    char *metadirn = path_append(META_DIR, dirn);
    char *path = path_append(metadirn, ".LinkTable");
    FILE *fp = fopen(path, "r");
    FREE(metadirn);

    if (!fp) {
        FREE(path);
        return NULL;
    }

    LinkTable *linktbl = CALLOC(1, sizeof(LinkTable));
    int sz = 0;
    if (fread(&sz, sizeof(int), 1, fp) != 1
        || fread(&linktbl->index_time, sizeof(time_t), 1, fp) != 1) {
        lprintf(error, "Failed to read the header of %s!\n", path);
        fclose(fp);
        LinkTable_free(linktbl);
        LinkTable_disk_delete(dirn);
        FREE(path);
        return NULL;
    }

    long entry_size = (long)(NAME_MAX + PATH_MAX + sizeof(LinkType)
                             + sizeof(size_t) + sizeof(long));
    if (fseek(fp, 0, SEEK_END) != 0) {
        lprintf(error, "Failed to seek %s!\n", path);
        fclose(fp);
        LinkTable_free(linktbl);
        LinkTable_disk_delete(dirn);
        FREE(path);
        return NULL;
    }
    long file_size = ftell(fp);
    if (file_size < 0
        || fseek(fp, (long)(sizeof(int) + sizeof(time_t)), SEEK_SET) != 0) {
        lprintf(error, "Failed to inspect %s!\n", path);
        fclose(fp);
        LinkTable_free(linktbl);
        LinkTable_disk_delete(dirn);
        FREE(path);
        return NULL;
    }

    long max_entries
        = (file_size - (long)(sizeof(int) + sizeof(time_t))) / entry_size;

    if (sz < 1 || max_entries < sz) {
        lprintf(error, "Invalid link table size: %d in %s!\n", sz, path);
        fclose(fp);
        LinkTable_free(linktbl);
        LinkTable_disk_delete(dirn);
        FREE(path);
        return NULL;
    }

    linktbl->size = sz;
    linktbl->links
        = (Link **)CALLOC( // NOLINT(clang-analyzer-optin.taint.TaintedAlloc)
            sz, sizeof(Link *));

    for (int i = 0; i < sz; i++) {
        linktbl->links[i] = CALLOC(1, sizeof(Link));
        linktbl->links[i]->parent_table = linktbl;
        if (fread(linktbl->links[i]->linkname, sizeof(char), NAME_MAX, fp)
                != NAME_MAX
            || fread(linktbl->links[i]->f_url, sizeof(char), PATH_MAX, fp)
                   != PATH_MAX
            || fread(&linktbl->links[i]->type, sizeof(LinkType), 1, fp) != 1
            || fread(&linktbl->links[i]->content_length, sizeof(size_t), 1, fp)
                   != 1
            || fread(&linktbl->links[i]->time, sizeof(long), 1, fp) != 1) {
            lprintf(error, "Corrupted LinkTable at index %d!\n", i);
            fclose(fp);
            LinkTable_free(linktbl);
            LinkTable_disk_delete(dirn);
            FREE(path);
            return NULL;
        }
    }
    if (fclose(fp)) {
        lprintf(error, "cannot close the file pointer, %s\n", strerror(errno));
    }

    FREE(path);
    return linktbl;
}

LinkTable *path_to_LinkTable(const char *path)
{
    Link *link = NULL;
    Link *tmp_link = NULL;
    LinkTable *next_table = NULL;

    if (!strcmp(path, "/")) {
        next_table = ROOT_LINK_TBL;
        LinkTable_ref(next_table);
        return next_table;
    } else {
        link = path_to_Link(path);
        if (!link) {
            return NULL;
        }
        tmp_link = link;

        PTHREAD_MUTEX_LOCK(&link_lock);
        next_table = link->next_table;
        if (next_table) {
            next_table->refcount++;
            next_table->orphaned = 0;
        }
        PTHREAD_MUTEX_UNLOCK(&link_lock);
    }

    if (!next_table) {
        LinkTable *new_table = NULL;
        if (CONFIG.mode == NORMAL) {
            new_table = LinkTable_new(tmp_link->f_url);
        } else if (CONFIG.mode == SINGLE) {
            new_table = single_LinkTable_new(tmp_link->f_url);
        } else if (CONFIG.mode == SONIC) {
            if (!CONFIG.sonic_id3) {
                new_table = sonic_LinkTable_new_index(tmp_link->sonic.id);
            } else {
                new_table = sonic_LinkTable_new_id3(tmp_link->sonic.depth,
                                                    tmp_link->sonic.id);
            }
        } else {
            lprintf(fatal, "Invalid CONFIG.mode: %d\n", CONFIG.mode);
        }

        if (!new_table) {
            if (link) {
                LinkTable_unref(link->parent_table);
            }
            return NULL;
        }

        PTHREAD_MUTEX_LOCK(&link_lock);
        if (!link->next_table) {
            link->next_table = new_table;
            new_table->parent_tbl = link->parent_table;
            new_table->parent_link = link;
            if (new_table->parent_tbl) {
                new_table->parent_tbl->refcount++;
            }
            new_table->refcount++;
            new_table->orphaned = 0;
            next_table = new_table;
        } else {
            LinkTable_free(new_table);
            next_table = link->next_table;
            next_table->refcount++;
            next_table->orphaned = 0;
        }
        PTHREAD_MUTEX_UNLOCK(&link_lock);

        if (CONFIG.invalid_refresh) {
            LinkTable_uninitialised_fill(next_table);
        }
    }

    if (link) {
        LinkTable_unref(link->parent_table);
    }

    return next_table;
}

static Link *path_to_Link_recursive(char *path, LinkTable *linktbl)
{
    if (!linktbl || !path || path[0] == '\0') {
        return NULL;
    }

    /*
     * skip the leading '/' if it exists
     */
    if (*path == '/') {
        path++;
    }

    /*
     * remove the last '/' if it exists
     */
    size_t path_len = strnlen(path, PATH_MAX);
    if (path_len > 0) {
        char *slash = &(path[path_len - 1]);
        if (*slash == '/') {
            *slash = '\0';
        }
    }

    char *slash = strchr(path, '/');
    if (slash == NULL) {
        /*
         * We cannot find another '/', we have reached the last level
         */
        for (int i = 1; i < linktbl->size; i++) {
            if (!strncmp(path, linktbl->links[i]->linkname, NAME_MAX)) {
                /*
                 * We found our link
                 */
                return linktbl->links[i];
            }
        }
    } else {
        /*
         * We can still find '/', time to consume the path and traverse
         * the tree structure
         */

        /*
         * add termination mark to the current string,
         * effective create two substrings
         */
        *slash = '\0';
        /*
         * move the pointer past the '/'
         */
        char *next_path = slash + 1;
        for (int i = 1; i < linktbl->size; i++) {
            if (!strncmp(path, linktbl->links[i]->linkname, NAME_MAX)) {
                /*
                 * The next sub-directory exists
                 */
                LinkTable *next_table = linktbl->links[i]->next_table;
                if (!next_table) {
                    linktbl->refcount++;
                    PTHREAD_MUTEX_UNLOCK(&link_lock);
                    LinkTable *new_table = NULL;
                    if (CONFIG.mode == NORMAL) {
                        new_table = LinkTable_new(linktbl->links[i]->f_url);
                    } else if (CONFIG.mode == SONIC) {
                        if (!CONFIG.sonic_id3) {
                            new_table = sonic_LinkTable_new_index(
                                linktbl->links[i]->sonic.id);
                        } else {
                            new_table = sonic_LinkTable_new_id3(
                                linktbl->links[i]->sonic.depth,
                                linktbl->links[i]->sonic.id);
                        }
                    } else {
                        lprintf(fatal, "Invalid CONFIG.mode\n");
                    }

                    if (!new_table) {
                        PTHREAD_MUTEX_LOCK(&link_lock);
                        linktbl->refcount--;
                        if (linktbl->refcount == 0 && linktbl->orphaned) {
                            LinkTable *parent = linktbl->parent_tbl;
                            Link *parent_link = linktbl->parent_link;
                            if (parent_link) {
                                parent_link->next_table = NULL;
                            }
                            PTHREAD_MUTEX_UNLOCK(&link_lock);
                            LinkTable_free(linktbl);
                            if (parent) {
                                LinkTable_unref(parent);
                            }
                            PTHREAD_MUTEX_LOCK(&link_lock);
                        }
                        return NULL;
                    }

                    PTHREAD_MUTEX_LOCK(&link_lock);
                    if (!linktbl->links[i]->next_table) {
                        linktbl->links[i]->next_table = new_table;
                        new_table->parent_tbl = linktbl;
                        new_table->parent_link = linktbl->links[i];
                        linktbl->refcount++;
                        next_table = new_table;
                    } else {
                        LinkTable_free(new_table);
                        next_table = linktbl->links[i]->next_table;
                    }
                    linktbl->refcount--;
                }
                return path_to_Link_recursive(next_path, next_table);
            }
        }
    }
    return NULL;
}

Link *path_to_Link(const char *path)
{
    lprintf(link_lock_debug, "thread %lx: locking link_lock;\n",
            (unsigned long)pthread_self());

    PTHREAD_MUTEX_LOCK(&link_lock);
    char *new_path = STRNDUP(path, PATH_MAX);
    if (!new_path) {
        lprintf(fatal, "cannot allocate memory\n");
    }
    Link *link = path_to_Link_recursive(new_path, ROOT_LINK_TBL);
    FREE(new_path);

    if (link && link->parent_table) {
        link->parent_table->refcount++;
    }

    lprintf(link_lock_debug, "thread %lx: unlocking link_lock;\n",
            (unsigned long)pthread_self());
    PTHREAD_MUTEX_UNLOCK(&link_lock);
    return link;
}

TransferStruct Link_download_full(Link *link)
{
    char *url = link->f_url;
    CURL *curl = Link_to_curl(link);

    TransferStruct ts = {0};
    ts.type = DATA;
    ts.transferring = 1;

    CURLcode ret = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&ts);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_PRIVATE, (void *)&ts);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }

    /*
     * If we get temporary HTTP failure, wait for 5 seconds before retry
     */
    long http_resp = 0;
    do {
        /*
         * Reset the transfer struct for each attempt to avoid accumulating
         * data from failed/partial attempts.
         */
        FREE(ts.data);
        ts.curr_size = 0;
        ts.transferring = 1;

        transfer_blocking(curl);
        ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
        if (HTTP_temp_failure(http_resp)) {
            lprintf(warning, "URL: %s, HTTP %ld, retrying later.\n", url,
                    http_resp);
            sleep(CONFIG.http_wait_sec);
        } else if (http_resp != HTTP_OK) {
            lprintf(warning, "cannot retrieve URL: %s, HTTP %ld\n", url,
                    http_resp);
            ts.curr_size = 0;
            free(ts.data); /* not FREE(); can be NULL on error path! */
            curl_easy_cleanup(curl);
            return ts;
        }
    } while (HTTP_temp_failure(http_resp));

    ret = curl_easy_getinfo(curl, CURLINFO_FILETIME, &(link->time));
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    curl_easy_cleanup(curl);
    return ts;
}

static CURL *Link_download_curl_setup(Link *link, size_t req_size, off_t offset,
                                      TransferStruct *header,
                                      TransferStruct *ts)
{
    if (!link) {
        lprintf(fatal, "Invalid supplied\n");
    }

    size_t start = offset;
    size_t end = start + req_size - 1;

    char range_str[64];
    snprintf(range_str, sizeof(range_str), "%lu-%lu", start, end);
    CURL *curl = Link_to_curl(link);
    CURLcode ret = curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)header);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)ts);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_PRIVATE, (void *)ts);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_RANGE, range_str);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }

    return curl;
}

static curl_off_t Link_download_cleanup(CURL *curl, TransferStruct *header)
{
    /*
     * Check for range seek support
     */
    if (!CONFIG.no_range_check) {
        if (!strcasestr((header->data), "Accept-Ranges: bytes")
            && !strcasestr((header->data), "Content-Range: bytes")) {
            fprintf(stderr, "This web server does not support HTTP range \
requests. If you do not believe that is the case, and if you plan to file a \
bug report, please include the following HTTP header information:\n%s\n",
                    header->data);
            exit(EXIT_FAILURE);
        }
    }

    FREE(header->data);

    long http_resp;
    CURLcode ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    curl_off_t recv = -1;
    if ((http_resp == HTTP_OK) || (http_resp == HTTP_PARTIAL_CONTENT)
        || (http_resp == HTTP_RANGE_NOT_SATISFIABLE)) {
        ret = curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &recv);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    } else {
        char *url;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);
        lprintf(warning, "Could not download %s, HTTP %ld\n", url, http_resp);
        if (HTTP_temp_failure(http_resp)) {
            recv = -EAGAIN;
        } else {
            recv = -ENOENT;
        }
    }

    curl_easy_cleanup(curl);

    return recv;
}

static void Link_download_finish_transfer(Cache *cf, off_t offset,
                                          TransferStruct *ts)
{
    if (!cf) {
        ts->transferring = 0;
        return;
    }

    PTHREAD_MUTEX_LOCK(&cf->dl_lock);
    ts->transferring = 0;
    ActiveDownload *ad = ActiveDownload_find(cf, offset);
    if (ad && ad->ts == ts) {
        ad->ts = NULL;
    }
    if (ts->ad_ptr) {
        ActiveDownload_unref(ts->ad_ptr);
        ts->ad_ptr = NULL;
    }
    PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
}

long Link_download(Link *link, char *output_buf, size_t req_size, off_t offset,
                   Cache *cf)
{
    if (req_size == 0 || link->content_length == 0 || offset < 0
        || (size_t)offset >= link->content_length) {
        return 0;
    }

    TransferStruct ts = {0};
    TransferStruct header = {0};
    curl_off_t recv_sz;

    size_t remaining = link->content_length - (size_t)offset;
    if (req_size > remaining) {
        lprintf(info, "requested size larger than remaining size, req_size: \
%zu, remaining: %zu\n",
                req_size, remaining);
        req_size = remaining;
    }

    do {
        ts.curr_size = 0;
        ts.data = NULL;
        ts.type = DATA;
        ts.transferring = 1;
        ts.cache_ptr = cf;
        ts.ad_ptr = NULL;

        if (cf) {
            PTHREAD_MUTEX_LOCK(&cf->dl_lock);
            ActiveDownload *ad = ActiveDownload_find(cf, offset);
            if (ad) {
                ad->ts = &ts;
                ts.ad_ptr = ad;
                ad->refcount++;
                PTHREAD_COND_BROADCAST(&ad->cond);
            }
            PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
        }

        header.curr_size = 0;
        header.data = NULL;
        header.cache_ptr = NULL;

        CURL *curl
            = Link_download_curl_setup(link, req_size, offset, &header, &ts);

        transfer_blocking(curl);

        recv_sz = Link_download_cleanup(curl, &header);

        if (recv_sz < 0) {
            Link_download_finish_transfer(cf, offset, &ts);
            FREE(ts.data);
            if (recv_sz == -EAGAIN) {
                lprintf(warning, "HTTP temporary failure, retrying...\n");
                sleep(CONFIG.http_wait_sec);
                continue;
            }
            return recv_sz;
        }

        if (recv_sz != (long int)req_size) {
            lprintf(error,
                    "req_size != recv, req_size: %lu, recv: %ld, retrying...\n",
                    req_size, recv_sz);
            Link_download_finish_transfer(cf, offset, &ts);
            FREE(ts.data);
            sleep(CONFIG.http_wait_sec);
            continue;
        }

        /* success */
        break;
    } while (1);

    Link_download_finish_transfer(cf, offset, &ts);

    memmove(output_buf, ts.data, recv_sz);
    FREE(ts.data);

    return recv_sz;
}

long path_download(const char *path, char *output_buf, size_t req_size,
                   off_t offset)
{
    if (!path) {
        lprintf(fatal, "NULL path supplied\n");
    }

    Link *link;
    link = path_to_Link(path);
    if (!link) {
        return -ENOENT;
    }

    long res = Link_download(link, output_buf, req_size, offset, NULL);
    LinkTable_unref(link->parent_table);
    return res;
}

static void make_link_relative(const char *page_url, char *link_url)
{
    /*
      Some servers make the links to subdirectories absolute (in URI terms:
      path-absolute), but our code expects them to be relative (in URI terms:
      path-noscheme), so change the contents of link_url as needed to
      accommodate that.

      Also, some servers serve their links as `./name`. This is helpful to
      them because it is the only way to express relative references when the
      first new path segment of the target contains an unescaped colon (`:`),
      eg in `./6:1-balun.png`. While stripping the ./ strictly speaking
      reintroduces that ambiguity, it is of little practical concern in this
      implementation, as full URI link targets are filtered by their number of
      slashes anyway. In URI terms, this converts path-noscheme with a leading
      `.` segment into path-noscheme or path-rootless without that segment.
    */

    if (link_url[0] == '.' && link_url[1] == '/') {
        memmove(link_url, link_url + 2, strlen(link_url) - 1);
        return;
    }

    if (link_url[0] != '/') {
        /* Already relative, nothing to do here!

          (Full URIs, eg. `http://example.com/path`, pass through here
          unmodified, but those are classified in different LinkTypes later
          anyway).
         */
        return;
    }

    /* Find the slash after the host name. */
    int slashes_left_to_find = 3;
    while (*page_url) {
        if (*page_url == '/' && !--slashes_left_to_find) {
            break;
        }
        /* N.B. This is here, rather than doing `while (*page_url++)`, because
           when we're done we want the pointer to point at the final slash. */
        page_url++;
    }
    if (slashes_left_to_find) {
        if (slashes_left_to_find == 1 && !*page_url) {
            /* We're at the top level of the web site and the user entered the
               URL without a trailing slash. */
            page_url = "/";
        } else {
            /* Well, that's odd. Let's return rather than trying to dig
               ourselves deeper into whatever hole we're in. */
            return;
        }
    }
    /* The page URL is no longer the full page_url, it's just the part after
       the host name. */
    /* The link URL should start with the page URL. */
    if (strstr(link_url, page_url) != link_url) {
        return;
    }
    int skip_len = strlen(page_url);
    if (page_url[skip_len - 1] != '/') {
        if (page_url[skip_len] != '/') {
            /* Um, I'm not sure what to do here, so give up. */
            return;
        }
        skip_len++;
    }
    /* Move the part of the link URL after the parent page's pat to
       the beginning of the link URL string, discarding what came
       before it. */
    memmove(link_url, link_url + skip_len, strlen(link_url) - skip_len + 1);
}
