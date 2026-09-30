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
    if (!ROOT_LINK_TBL || !ROOT_LINK_TBL->links || !ROOT_LINK_TBL->links[0]) {
        return 1;
    }
    if (!CONFIG.external_links && !CONFIG.advanced_parsing_mode) {
        return 1;
    }
    return !is_cross_origin(ROOT_LINK_TBL->links[0]->f_url, link_url);
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
    LinkTable_add_diagnostics(linktbl, NULL, 0, NULL, 0);
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
        ROOT_LINK_TBL = LinkTable_new(url, NULL);
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

void LinkTable_add_diagnostics(LinkTable *linktbl, const char *content,
                               size_t content_len, const char *header,
                               size_t header_len)
{
    if (!linktbl) {
        return;
    }

    /* Check if .httpdirfs already exists */
    for (int i = 1; i < linktbl->size; i++) {
        if (!strcmp(linktbl->links[i]->linkname, ".httpdirfs")) {
            return;
        }
    }

    Link *diag_dir = Link_new(".httpdirfs", LINK_DIR);
    diag_dir->is_virtual = 1;
    diag_dir->time = linktbl->index_time;
    if (linktbl->size > 0 && linktbl->links && linktbl->links[0]) {
        snprintf(diag_dir->f_url, sizeof(diag_dir->f_url), "%s",
                 linktbl->links[0]->f_url);
    }

    LinkTable *diag_tbl = CALLOC(1, sizeof(LinkTable));
    diag_tbl->size = 0;
    diag_tbl->index_time = linktbl->index_time;
    diag_tbl->refcount = 1;
    diag_tbl->parent_tbl = linktbl;
    diag_tbl->parent_link = diag_dir;

    Link *diag_head = Link_new(".httpdirfs", LINK_HEAD);
    diag_head->is_virtual = 1;
    diag_head->time = linktbl->index_time;
    if (linktbl->size > 0 && linktbl->links && linktbl->links[0]) {
        snprintf(diag_head->f_url, sizeof(diag_head->f_url), "%s",
                 linktbl->links[0]->f_url);
    }
    LinkTable_add(diag_tbl, diag_head);

    Link *content_link = Link_new("CONTENT", LINK_FILE);
    content_link->is_virtual = 1;
    content_link->time = linktbl->index_time;
    content_link->content_length = content_len;
    if (content && content_len > 0) {
        content_link->virtual_content = CALLOC(1, content_len + 1);
        memcpy(content_link->virtual_content, content, content_len);
        content_link->virtual_content[content_len] = '\0';
    }
    LinkTable_add(diag_tbl, content_link);

    Link *header_link = Link_new("HEADER", LINK_FILE);
    header_link->is_virtual = 1;
    header_link->time = linktbl->index_time;
    header_link->content_length = header_len;
    if (header && header_len > 0) {
        header_link->virtual_content = CALLOC(1, header_len + 1);
        memcpy(header_link->virtual_content, header, header_len);
        header_link->virtual_content[header_len] = '\0';
    }
    LinkTable_add(diag_tbl, header_link);

    diag_dir->next_table = diag_tbl;
    LinkTable_add(linktbl, diag_dir);
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
    size_t len_a = strnlen(str_a, PATH_MAX);
    size_t len_b = strnlen(str_b, PATH_MAX);
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
    size_t len = strnlen(str, PATH_MAX);

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
            FREE(set->buckets[i]);
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

static int parse_origin(const char *url, size_t origin_len, char *scheme,
                        size_t scheme_max, char *host, size_t host_max,
                        int *port)
{
    if (!url || origin_len == 0) {
        return -1;
    }

    // Find "://" within origin_len
    const char *scheme_end = NULL;
    for (size_t i = 0; i + 2 < origin_len; i++) {
        if (url[i] == ':' && url[i + 1] == '/' && url[i + 2] == '/') {
            scheme_end = url + i;
            break;
        }
    }

    if (!scheme_end) {
        return -1; // malformed origin
    }

    size_t scheme_len = (size_t)(scheme_end - url);
    if (scheme_len >= scheme_max) {
        return -1;
    }
    strncpy(scheme, url, scheme_len);
    scheme[scheme_len] = '\0';
    // Lowercase scheme
    for (size_t i = 0; i < scheme_len; i++) {
        if (scheme[i] >= 'A' && scheme[i] <= 'Z') {
            scheme[i] += 32;
        }
    }

    const char *host_start = scheme_end + 3;
    const char *origin_end = url + origin_len;
    if (host_start >= origin_end) {
        return -1;
    }

    size_t host_part_len = (size_t)(origin_end - host_start);

    // Check for IPv6 bracketed host
    const char *bracket_close = NULL;
    if (*host_start == '[') {
        for (const char *p = host_start; p < origin_end; p++) {
            if (*p == ']') {
                bracket_close = p;
                break;
            }
        }
    }

    const char *port_colon = NULL;
    if (bracket_close) {
        // Look for colon after the closing bracket
        for (const char *p = bracket_close + 1; p < origin_end; p++) {
            if (*p == ':') {
                port_colon = p;
                break;
            }
        }
    } else {
        // Look for colon in the host part (there should be at most one colon if
        // no brackets)
        for (const char *p = host_start; p < origin_end; p++) {
            if (*p == ':') {
                port_colon = p;
                break;
            }
        }
    }

    size_t host_len;
    if (port_colon) {
        host_len = (size_t)(port_colon - host_start);
        // Parse port
        int p_val = 0;
        for (const char *p = port_colon + 1; p < origin_end; p++) {
            if (*p >= '0' && *p <= '9') {
                p_val = p_val * 10 + (*p - '0');
            } else {
                return -1; // invalid character in port
            }
        }
        *port = p_val;
    } else {
        host_len = host_part_len;
        // Default port based on scheme
        if (strcmp(scheme, "http") == 0) {
            *port = 80;
        } else if (strcmp(scheme, "https") == 0) {
            *port = 443;
        } else {
            *port = 0; // unknown scheme default port
        }
    }

    if (host_len >= host_max) {
        return -1;
    }
    strncpy(host, host_start, host_len);
    host[host_len] = '\0';
    // Lowercase host
    for (size_t i = 0; i < host_len; i++) {
        if (host[i] >= 'A' && host[i] <= 'Z') {
            host[i] += 32;
        }
    }

    return 0;
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
    while (*p && slashes < 3 && *p != '?' && *p != '#') {
        if (*p == '/') {
            slashes++;
        }
        p++;
    }
    size_t page_origin_len;
    if (slashes < 3) {
        if (is_external_url(page_url)) {
            const char *q = strpbrk(page_url, "?#");
            page_origin_len = q ? (size_t)(q - page_url) : strlen(page_url);
        } else {
            return 1; /* malformed page_url */
        }
    } else {
        page_origin_len = (size_t)(p - page_url) - 1;
    }

    slashes = 0;
    const char *l = link_url;
    while (*l && slashes < 3 && *l != '?' && *l != '#') {
        if (*l == '/') {
            slashes++;
        }
        l++;
    }
    size_t link_origin_len;
    if (slashes < 3) {
        if (is_external_url(link_url)) {
            const char *q = strpbrk(link_url, "?#");
            link_origin_len = q ? (size_t)(q - link_url) : strlen(link_url);
        } else {
            return 1; /* malformed link_url */
        }
    } else {
        link_origin_len = (size_t)(l - link_url) - 1;
    }

    char page_scheme[32];
    char page_host[PATH_MAX];
    int page_port = 0;
    if (parse_origin(page_url, page_origin_len, page_scheme,
                     sizeof(page_scheme), page_host, sizeof(page_host),
                     &page_port)
        != 0) {
        return 1;
    }

    char link_scheme[32];
    char link_host[PATH_MAX];
    int link_port = 0;
    if (parse_origin(link_url, link_origin_len, link_scheme,
                     sizeof(link_scheme), link_host, sizeof(link_host),
                     &link_port)
        != 0) {
        return 1;
    }

    if (strcmp(page_scheme, link_scheme) != 0) {
        return 1;
    }
    if (strcmp(page_host, link_host) != 0) {
        return 1;
    }
    if (page_port != link_port) {
        return 1;
    }

    return 0;
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
    for (char *ptr = result; *ptr; ptr++) {
        if (*ptr == '/') {
            *ptr = '_';
        }
    }
    return result;
}

static void normalize_url_path(char *url)
{
    if (!url) {
        return;
    }
    char *scheme_sep = strstr(url, "://");
    if (!scheme_sep) {
        return;
    }
    char *path_start = strchr(scheme_sep + 3, '/');
    if (!path_start) {
        return;
    }

    /* Fast check if any dot segments exist */
    if (!strstr(path_start, "/.") && strcmp(path_start, "/.") != 0
        && strcmp(path_start, "/..") != 0) {
        return;
    }

    char *qf = strpbrk(path_start, "?#");
    char saved_qf_char = '\0';
    if (qf) {
        saved_qf_char = *qf;
        *qf = '\0';
    }

    size_t orig_len = strlen(path_start);
    int trailing_slash = (orig_len > 0 && path_start[orig_len - 1] == '/');

    const char *seg_start[256];
    size_t seg_len[256];
    int nsegs = 0;

    const char *p = path_start;
    while (*p) {
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        const char *next = strchr(p, '/');
        size_t len = next ? (size_t)(next - p) : strlen(p);

        if (len == 1 && p[0] == '.') {
            if (!next) {
                trailing_slash = 1;
            }
        } else if (len == 2 && p[0] == '.' && p[1] == '.') {
            if (nsegs > 0) {
                nsegs--;
            }
            if (!next) {
                trailing_slash = 1;
            }
        } else {
            if (nsegs < 256) {
                seg_start[nsegs] = p;
                seg_len[nsegs] = len;
                nsegs++;
            }
        }

        if (next) {
            p = next;
        } else {
            break;
        }
    }

    char *dest = path_start;
    *dest++ = '/';
    for (int i = 0; i < nsegs; i++) {
        memmove(dest, seg_start[i], seg_len[i]);
        dest += seg_len[i];
        if (i < nsegs - 1 || trailing_slash) {
            *dest++ = '/';
        }
    }
    *dest = '\0';

    if (qf) {
        *qf = saved_qf_char;
        size_t qf_len = strlen(qf);
        memmove(dest, qf, qf_len + 1);
    }
}

int resolve_target_url(const char *page_url, const char *raw_href,
                       char *out_url, size_t out_size)
{
    if (!page_url || !raw_href || !out_url || out_size == 0) {
        return 0;
    }

    /* Trim leading and trailing whitespace from raw_href */
    while (*raw_href && isspace((unsigned char)*raw_href)) {
        raw_href++;
    }
    if (*raw_href == '\0') {
        return 0;
    }
    size_t href_len = strlen(raw_href);
    while (href_len > 0 && isspace((unsigned char)raw_href[href_len - 1])) {
        href_len--;
    }

    /* Ignore fragment in href */
    const char *hash_pos = memchr(raw_href, '#', href_len);
    if (hash_pos) {
        href_len = (size_t)(hash_pos - raw_href);
    }
    if (href_len == 0) {
        return 0;
    }

    char resolved[PATH_MAX + 1];
    size_t resolved_len = 0;

    /* 1. Absolute URL (http:// or https://) */
    if ((href_len >= 7 && strncasecmp(raw_href, "http://", 7) == 0)
        || (href_len >= 8 && strncasecmp(raw_href, "https://", 8) == 0)) {
        if (href_len >= sizeof(resolved)) {
            return 0;
        }
        memcpy(resolved, raw_href, href_len);
        resolved[href_len] = '\0';
        resolved_len = href_len;
    } else if (href_len >= 2 && raw_href[0] == '/' && raw_href[1] == '/') {
        /* 2. Scheme-relative URL (//...) */
        const char *colon = strchr(page_url, ':');
        if (!colon) {
            return 0;
        }
        size_t scheme_len = (size_t)(colon - page_url) + 1; /* includes ':' */
        if (scheme_len + href_len >= sizeof(resolved)) {
            return 0;
        }
        memcpy(resolved, page_url, scheme_len);
        memcpy(resolved + scheme_len, raw_href, href_len);
        resolved[scheme_len + href_len] = '\0';
        resolved_len = scheme_len + href_len;
    } else if (raw_href[0] == '/') {
        /* 3. Origin-relative URL (/...) */
        const char *scheme_sep = strstr(page_url, "://");
        if (!scheme_sep) {
            return 0;
        }
        const char *origin_end = strchr(scheme_sep + 3, '/');
        size_t origin_len
            = origin_end ? (size_t)(origin_end - page_url) : strlen(page_url);
        if (origin_len + href_len >= sizeof(resolved)) {
            return 0;
        }
        memcpy(resolved, page_url, origin_len);
        memcpy(resolved + origin_len, raw_href, href_len);
        resolved[origin_len + href_len] = '\0';
        resolved_len = origin_len + href_len;
    } else {
        /* 4. Path-relative URL (does not start with '/') */
        size_t page_len = strlen(page_url);
        size_t base_len = 0;
        if (page_url[page_len - 1] == '/') {
            base_len = page_len;
        } else {
            const char *scheme_sep = strstr(page_url, "://");
            const char *last_slash = strrchr(page_url, '/');
            if (scheme_sep && last_slash && last_slash > scheme_sep + 2) {
                base_len = (size_t)(last_slash - page_url) + 1;
            } else {
                base_len = page_len;
            }
        }

        int need_slash = (page_url[base_len - 1] != '/');
        if (base_len + (need_slash ? 1 : 0) + href_len >= sizeof(resolved)) {
            return 0;
        }

        memcpy(resolved, page_url, base_len);
        if (need_slash) {
            resolved[base_len] = '/';
            base_len++;
        }
        memcpy(resolved + base_len, raw_href, href_len);
        resolved[base_len + href_len] = '\0';
        resolved_len = base_len + href_len;
    }

    /* Encode spaces as %20 so that libcurl and web servers can parse the URL */
    size_t out_pos = 0;
    for (size_t i = 0; i < resolved_len; i++) {
        if (resolved[i] == ' ') {
            if (out_pos + 3 >= out_size) {
                return 0;
            }
            out_url[out_pos++] = '%';
            out_url[out_pos++] = '2';
            out_url[out_pos++] = '0';
        } else {
            if (out_pos + 1 >= out_size) {
                return 0;
            }
            out_url[out_pos++] = resolved[i];
        }
    }
    out_url[out_pos] = '\0';
    normalize_url_path(out_url);
    return 1;
}

static void collect_gumbo_text(const GumboNode *node, char **buf, size_t *len,
                               size_t *cap)
{
    if (!node) {
        return;
    }
    if (node->type == GUMBO_NODE_TEXT || node->type == GUMBO_NODE_WHITESPACE) {
        const char *t = node->v.text.text;
        if (t) {
            size_t tlen = strlen(t);
            if (*len + tlen + 1 > *cap) {
                *cap = (*cap == 0) ? 256 : ((*cap * 2) + tlen + 1);
                *buf = (char *)REALLOC(*buf, *cap);
            }
            memcpy(*buf + *len, t, tlen);
            *len += tlen;
            (*buf)[*len] = '\0';
        }
    } else if (node->type == GUMBO_NODE_ELEMENT) {
        const GumboVector *children = &node->v.element.children;
        for (size_t i = 0; i < children->length; ++i) {
            collect_gumbo_text((const GumboNode *)children->data[i], buf, len,
                               cap);
        }
    }
}

char *extract_anchor_text(const GumboNode *node)
{
    if (!node) {
        return STRDUP("");
    }
    char *raw = NULL;
    size_t len = 0;
    size_t cap = 0;
    collect_gumbo_text(node, &raw, &len, &cap);
    if (!raw || len == 0) {
        FREE(raw);
        return STRDUP("");
    }

    char *src = raw;
    char *dst = raw;
    int in_space = 0;

    /* Skip leading whitespace */
    int sp = 0;
    while ((sp = check_space(src)) > 0) {
        src += sp;
    }

    while (*src) {
        sp = check_space(src);
        if (sp > 0) {
            if (!in_space) {
                *dst++ = ' ';
                in_space = 1;
            }
            src += sp;
            continue;
        }
        unsigned char c = (unsigned char)*src;
        if (c == '/') {
            *dst++ = '_';
            in_space = 0;
        } else if (c >= 32 && c != 127) {
            *dst++ = (char)c;
            in_space = 0;
        }
        src++;
    }

    /* Strip trailing whitespace */
    while (dst > raw && isspace((unsigned char)*(dst - 1))) {
        dst--;
    }
    *dst = '\0';

    char *result = STRDUP(raw);
    FREE(raw);
    return result;
}

int extract_url_path_segments(const char *url, char ***segments_out,
                              int *num_segments_out)
{
    if (!segments_out || !num_segments_out) {
        return 0;
    }
    *segments_out = NULL;
    *num_segments_out = 0;

    if (!url || *url == '\0') {
        return 0;
    }

    /* 1. Strip query parameters and fragment */
    const char *qf = strpbrk(url, "?#");
    size_t url_len = qf ? (size_t)(qf - url) : strlen(url);

    /* 2. Skip scheme and host to isolate path */
    const char *path = url;
    const char *scheme_sep = strstr(url, "://");
    if (scheme_sep && (size_t)(scheme_sep - url) < url_len) {
        const char *after_host = strchr(scheme_sep + 3, '/');
        if (after_host && (!qf || after_host < qf)) {
            path = after_host;
        } else {
            return 0;
        }
    } else if (url_len >= 2 && url[0] == '/' && url[1] == '/') {
        const char *after_host = strchr(url + 2, '/');
        if (after_host && (!qf || after_host < qf)) {
            path = after_host;
        } else {
            return 0;
        }
    }

    size_t path_len = qf ? (size_t)(qf - path) : strlen(path);

    /* 3. Strip trailing slashes */
    while (path_len > 0 && path[path_len - 1] == '/') {
        path_len--;
    }
    if (path_len == 0) {
        return 0;
    }

    /* 4. Count non-empty segments */
    int count = 0;
    size_t i = 0;
    while (i < path_len) {
        while (i < path_len && path[i] == '/') {
            i++;
        }
        if (i < path_len) {
            count++;
            while (i < path_len && path[i] != '/') {
                i++;
            }
        }
    }

    if (count == 0) {
        return 0;
    }

    char **segs = (char **)CALLOC((size_t)count, sizeof(char *));
    int idx = 0;
    i = 0;
    while (i < path_len && idx < count) {
        while (i < path_len && path[i] == '/') {
            i++;
        }
        if (i < path_len) {
            size_t start = i;
            while (i < path_len && path[i] != '/') {
                i++;
            }
            size_t seg_len = i - start;

            int unescaped_len = 0;
            char *unescaped = curl_easy_unescape(NULL, path + start,
                                                 (int)seg_len, &unescaped_len);
            char *seg_str = NULL;
            if (unescaped) {
                seg_str = STRNDUP(unescaped, (size_t)unescaped_len);
                curl_free(unescaped);
            } else {
                seg_str = STRNDUP(path + start, seg_len);
            }

            for (char *c = seg_str; *c; c++) {
                if (*c == '/') {
                    *c = '_';
                }
            }
            segs[idx++] = seg_str;
        }
    }

    *segments_out = segs;
    *num_segments_out = idx;
    return idx;
}

void free_url_path_segments(char **segments, int num_segments)
{
    if (!segments) {
        return;
    }
    for (int i = 0; i < num_segments; i++) {
        FREE(segments[i]);
    }
    FREE(segments);
}

char *generate_collision_free_name(LinkHashSet *set, const char *anchor,
                                   char **segments, int num_segments)
{
    if (!set) {
        return NULL;
    }

    char candidate[NAME_MAX + 1];

    if (num_segments <= 0) {
        if (!anchor || *anchor == '\0') {
            return NULL;
        }
        snprintf(candidate, sizeof(candidate), "%s", anchor);
        if (LinkHashSet_add(set, candidate)) {
            return STRDUP(candidate);
        }
        for (int suffix = 1; suffix < 10000; suffix++) {
            char suffix_str[32];
            int s_len = snprintf(suffix_str, sizeof(suffix_str), "-%d", suffix);
            char suffixed[NAME_MAX + 1];
            size_t base_len = strlen(candidate);
            if (base_len + (size_t)s_len >= sizeof(suffixed)) {
                base_len = sizeof(suffixed) - (size_t)s_len - 1;
            }
            snprintf(suffixed, sizeof(suffixed), "%.*s%s", (int)base_len,
                     candidate, suffix_str);
            if (LinkHashSet_add(set, suffixed)) {
                return STRDUP(suffixed);
            }
        }
        return NULL;
    }

    /* Iterate through path depths i = 1, 2, ..., num_segments */
    for (int i = 1; i <= num_segments; i++) {
        char path_part[NAME_MAX + 1];
        path_part[0] = '\0';
        size_t cur_len = 0;

        for (int j = num_segments - i; j < num_segments; j++) {
            const char *seg = segments[j];
            size_t seg_len = strlen(seg);
            if (cur_len > 0) {
                if (cur_len + 1 < sizeof(path_part)) {
                    path_part[cur_len++] = '-';
                    path_part[cur_len] = '\0';
                }
            }
            if (cur_len + seg_len < sizeof(path_part)) {
                memcpy(path_part + cur_len, seg, seg_len);
                cur_len += seg_len;
                path_part[cur_len] = '\0';
            }
        }

        const char *last_seg = segments[num_segments - 1];
        int omit_anchor = (!anchor || *anchor == '\0');
        if (i == 1 && anchor && *anchor != '\0'
            && strcasecmp(anchor, last_seg) == 0) {
            omit_anchor = 1;
        }

        if (omit_anchor) {
            snprintf(candidate, sizeof(candidate), "%s", path_part);
        } else {
            snprintf(candidate, sizeof(candidate), "%s-%s", anchor, path_part);
        }

        if (LinkHashSet_add(set, candidate)) {
            return STRDUP(candidate);
        }
    }

    /* Exhaustion fallback: append numeric suffix */
    char last_candidate[NAME_MAX + 1];
    snprintf(last_candidate, sizeof(last_candidate), "%s", candidate);
    for (int suffix = 1; suffix < 10000; suffix++) {
        char suffix_str[32];
        int s_len = snprintf(suffix_str, sizeof(suffix_str), "-%d", suffix);
        char suffixed[NAME_MAX + 1];
        size_t base_len = strlen(last_candidate);
        if (base_len + (size_t)s_len >= sizeof(suffixed)) {
            base_len = sizeof(suffixed) - (size_t)s_len - 1;
        }
        snprintf(suffixed, sizeof(suffixed), "%.*s%s", (int)base_len,
                 last_candidate, suffix_str);
        if (LinkHashSet_add(set, suffixed)) {
            return STRDUP(suffixed);
        }
    }

    return NULL;
}

int is_html_content_type(const char *ct)
{
    if (!ct) {
        return 0;
    }
    while (*ct && isspace((unsigned char)*ct)) {
        ct++;
    }
    if (strncasecmp(ct, "text/html", 9) == 0) {
        char next = ct[9];
        return (next == '\0' || next == ';' || isspace((unsigned char)next));
    }
    return 0;
}

LinkType Link_classify_response(LinkType current_type, long http_resp,
                                curl_off_t cl, const char *content_type,
                                size_t *content_len_out)
{
    if (http_resp != HTTP_OK) {
        return LINK_INVALID;
    }

    if (current_type == LINK_UNINITIALISED_DIR) {
        return LINK_DIR;
    }

    if (current_type != LINK_UNINITIALISED_FILE) {
        return current_type;
    }

    if (CONFIG.advanced_parsing_mode) {
        if (is_html_content_type(content_type)) {
            if (cl > 0 && (off_t)cl > CONFIG.max_html_size) {
                if (content_len_out) {
                    *content_len_out = (size_t)cl;
                }
                return LINK_FILE;
            }
            return LINK_DIR;
        }
        /* Non-HTML Content-Type (image, binary, ISO, etc.) */
        if (cl < 0) {
            return LINK_INVALID;
        }
        if (cl == 0 && CONFIG.zero_len_is_dir) {
            return LINK_DIR;
        }
        if (content_len_out) {
            *content_len_out = (size_t)cl;
        }
        return LINK_FILE;
    }

    /* Vanilla mode */
    if (cl < 0) {
        return LINK_INVALID;
    } else if (cl == 0 && CONFIG.zero_len_is_dir) {
        return LINK_DIR;
    } else {
        if (content_len_out) {
            *content_len_out = (size_t)cl;
        }
        return LINK_FILE;
    }
}

static int url_matches_head_link(const char *target_url, const char *head_url)
{
    if (!target_url || !head_url) {
        return 0;
    }

    char *unescaped_target = curl_easy_unescape(NULL, target_url, 0, NULL);
    char *unescaped_head = curl_easy_unescape(NULL, head_url, 0, NULL);
    const char *tgt = unescaped_target ? unescaped_target : target_url;
    const char *head = unescaped_head ? unescaped_head : head_url;

    const char *tgt_end = tgt + strlen(tgt);
    const char *head_end = head + strlen(head);

    /* If head_url has no query string, ignore query string on target_url */
    if (!strchr(head, '?')) {
        const char *q = strchr(tgt, '?');
        if (q) {
            tgt_end = q;
        }
    }

    /* Strip trailing slashes, but keep root slash after scheme://host */
    const char *tgt_slash = strstr(tgt, "://");
    const char *tgt_min = tgt_slash ? tgt_slash + 3 : tgt;
    const char *head_slash = strstr(head, "://");
    const char *head_min = head_slash ? head_slash + 3 : head;

    while (tgt_end > tgt_min && *(tgt_end - 1) == '/') {
        tgt_end--;
    }
    while (head_end > head_min && *(head_end - 1) == '/') {
        head_end--;
    }

    size_t tgt_len = (size_t)(tgt_end - tgt);
    size_t head_len = (size_t)(head_end - head);

    int match = (tgt_len == head_len && strncmp(tgt, head, tgt_len) == 0);

    if (unescaped_target) {
        curl_free(unescaped_target);
    }
    if (unescaped_head) {
        curl_free(unescaped_head);
    }
    return match;
}

int is_ancestor_head_link(const LinkTable *linktbl, const char *target_url)
{
    if (!linktbl || !target_url || target_url[0] == '\0') {
        return 0;
    }

    /* Check current folder's head link (prevent self-loops) */
    if (linktbl->links && linktbl->size > 0 && linktbl->links[0]) {
        if (url_matches_head_link(target_url, linktbl->links[0]->f_url)) {
            return 1;
        }
    }

    /* Check ancestor tables in parent_tbl chain */
    for (const LinkTable *cur = linktbl->parent_tbl; cur != NULL;
         cur = cur->parent_tbl) {
        if (cur->links && cur->size > 0 && cur->links[0]) {
            if (url_matches_head_link(target_url, cur->links[0]->f_url)) {
                return 1;
            }
        }
    }

    /* Check ROOT_LINK_TBL explicitly as safety fallback */
    if (ROOT_LINK_TBL && ROOT_LINK_TBL != linktbl && ROOT_LINK_TBL->links
        && ROOT_LINK_TBL->size > 0 && ROOT_LINK_TBL->links[0]) {
        if (url_matches_head_link(target_url, ROOT_LINK_TBL->links[0]->f_url)) {
            return 1;
        }
    }

    return 0;
}

static void process_anchor_node(const char *url, const GumboNode *node,
                                LinkTable *linktbl, LinkHashSet *set,
                                LinkHashSet *target_url_set)
{
    GumboAttribute *href
        = gumbo_get_attribute(&node->v.element.attributes, "href");
    if (!href) {
        return;
    }
    const char *raw_href = href->value;

    if (CONFIG.ignore_anchors && raw_href[0] == '#') {
        /* Skip intra-page HTML anchor / fragment links when requested */
        return;
    }

    char target_url[PATH_MAX + 1];
    int target_url_resolved
        = resolve_target_url(url, raw_href, target_url, sizeof(target_url));
    if (target_url_resolved && is_ancestor_head_link(linktbl, target_url)) {
        return;
    }

    if (CONFIG.advanced_parsing_mode) {
        if (target_url_resolved) {
            int allow = 1;
            if (CONFIG.same_origin_only) {
                const char *page_url = NULL;
                if (ROOT_LINK_TBL && ROOT_LINK_TBL->links
                    && ROOT_LINK_TBL->links[0]) {
                    page_url = ROOT_LINK_TBL->links[0]->f_url;
                } else if (linktbl && linktbl->links && linktbl->links[0]) {
                    page_url = linktbl->links[0]->f_url;
                } else {
                    page_url = url;
                }
                if (page_url && is_cross_origin(page_url, target_url)) {
                    allow = 0;
                }
            }
            if (allow) {
                /* Early duplicate target link removal (first anchor text
                 * wins) */
                if (!target_url_set
                    || LinkHashSet_add(target_url_set, target_url)) {
                    char *anchor = extract_anchor_text(node);
                    char **segments = NULL;
                    int num_segments = 0;
                    extract_url_path_segments(target_url, &segments,
                                              &num_segments);

                    char *linkname = generate_collision_free_name(
                        set, anchor, segments, num_segments);
                    FREE(anchor);
                    free_url_path_segments(segments, num_segments);

                    if (linkname && linkname[0] != '\0') {
                        const char *qf = strpbrk(target_url, "?#");
                        size_t tlen = qf ? (size_t)(qf - target_url)
                                         : strlen(target_url);
                        LinkType type
                            = (tlen > 0 && target_url[tlen - 1] == '/')
                                  ? LINK_UNINITIALISED_DIR
                                  : LINK_UNINITIALISED_FILE;
                        Link *link = Link_new(linkname, type);
                        snprintf(link->f_url, sizeof(link->f_url), "%s",
                                 target_url);
                        LinkTable_add(linktbl, link);
                        FREE(linkname);
                    }
                }
            }
        }
    } else if (CONFIG.external_links && is_external_url(raw_href)
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
            size_t href_len = qf ? (size_t)(qf - raw_href) : strlen(raw_href);
            LinkType type = (href_len > 0 && raw_href[href_len - 1] == '/')
                                ? LINK_UNINITIALISED_DIR
                                : LINK_UNINITIALISED_FILE;

            /* First-wins: skip if a link with this name already exists */
            if (LinkHashSet_add(set, filename)) {
                Link *link = Link_new(filename, type);
                if (target_url_resolved) {
                    snprintf(link->f_url, sizeof(link->f_url), "%s",
                             target_url);
                } else {
                    snprintf(link->f_url, sizeof(link->f_url), "%s", raw_href);
                }
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

/**
 * Recursively walk the HTML DOM tree to extract links into the link table.
 */
static void HTML_to_LinkTable(const char *url, GumboNode *node,
                              LinkTable *linktbl, LinkHashSet *set,
                              LinkHashSet *target_url_set)
{
    if (!node || node->type != GUMBO_NODE_ELEMENT) {
        return;
    }

    if (node->v.element.tag == GUMBO_TAG_A) {
        process_anchor_node(url, node, linktbl, set, target_url_set);
        /*
         * HTML5 interactive content cannot contain nested <a> elements, and
         * extract_anchor_text() already traversed any child text nodes.
         */
        return;
    }

    /* Note the recursive call */
    GumboVector *children = &node->v.element.children;
    for (size_t i = 0; i < children->length; ++i) {
        HTML_to_LinkTable(url, (GumboNode *)children->data[i], linktbl, set,
                          target_url_set);
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
    LinkHashSet *target_url_set
        = CONFIG.advanced_parsing_mode ? LinkHashSet_new(4096) : NULL;
    if (output->root) {
        HTML_to_LinkTable(url, output->root, linktbl, set, target_url_set);
    }
    if (target_url_set) {
        LinkHashSet_free(target_url_set);
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

        char *content_type = NULL;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type);

        size_t content_len = 0;
        this_link->type = Link_classify_response(this_link->type, http_resp, cl,
                                                 content_type, &content_len);
        if (this_link->type == LINK_FILE) {
            this_link->content_length = content_len;
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
            FREE(entry->virtual_content);
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

LinkTable *LinkTable_new(const char *url, LinkTable *parent_tbl)
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
                linktbl->parent_tbl = parent_tbl;
            }
        }
    }

    /*
     * Download a new LinkTable because we didn't manage to load it from the
     * disk
     */
    if (!linktbl) {
        linktbl = LinkTable_alloc(url);
        linktbl->parent_tbl = parent_tbl;
        linktbl->index_time = time(NULL);

        /*
         * start downloading the base URL
         */
        TransferStruct header_ts = {0};
        TransferStruct ts = Link_download_full(linktbl->links[0], &header_ts);
        if (ts.curr_size == 0) {
            FREE(header_ts.data);
            LinkTable_free(linktbl);
            return NULL;
        }

        /*
         * Otherwise parsed the received data
         */
        LinkTable_parse_html(linktbl, url, ts.data);

        LinkTable_fill(linktbl);

        LinkTable_add_diagnostics(linktbl, ts.data, ts.curr_size,
                                  header_ts.data, header_ts.curr_size);
        FREE(ts.data);
        FREE(header_ts.data);

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
    char *cpath = path_append(metadirn, ".httpdirfs_content");
    unlink(cpath);
    FREE(cpath);
    char *hpath = path_append(metadirn, ".httpdirfs_header");
    unlink(hpath);
    FREE(hpath);
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

    if (!fp) {
        lprintf(error, "fopen(%s): %s\n", path, strerror(errno));
        FREE(path);
        FREE(metadirn);
        return -1;
    }

    Link *diag_dir = NULL;
    int saved_size = 0;
    for (int i = 0; i < linktbl->size; i++) {
        if (!strcmp(linktbl->links[i]->linkname, ".httpdirfs")) {
            diag_dir = linktbl->links[i];
        } else {
            saved_size++;
        }
    }

    if (fwrite(&saved_size, sizeof(int), 1, fp) != 1
        || fwrite(&linktbl->index_time, sizeof(time_t), 1, fp) != 1) {
        lprintf(error, "Failed to save the header of %s!\n", path);
    }
    FREE(path);
    for (int i = 0; i < linktbl->size; i++) {
        if (!strcmp(linktbl->links[i]->linkname, ".httpdirfs")) {
            continue;
        }
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

    if (diag_dir && diag_dir->next_table) {
        LinkTable *dtbl = diag_dir->next_table;
        const char *cdata = NULL;
        size_t content_len = 0;
        const char *hdata = NULL;
        size_t header_len = 0;
        for (int i = 1; i < dtbl->size; i++) {
            if (!strcmp(dtbl->links[i]->linkname, "CONTENT")) {
                cdata = dtbl->links[i]->virtual_content;
                content_len = dtbl->links[i]->content_length;
            } else if (!strcmp(dtbl->links[i]->linkname, "HEADER")) {
                hdata = dtbl->links[i]->virtual_content;
                header_len = dtbl->links[i]->content_length;
            }
        }
        char *cpath = path_append(metadirn, ".httpdirfs_content");
        char *hpath = path_append(metadirn, ".httpdirfs_header");
        FILE *cfp = fopen(cpath, "wb");
        if (cfp) {
            if (cdata && content_len > 0) {
                ignore_value(fwrite(cdata, 1, content_len, cfp));
            }
            fclose(cfp);
        }
        FILE *hfp = fopen(hpath, "wb");
        if (hfp) {
            if (hdata && header_len > 0) {
                ignore_value(fwrite(hdata, 1, header_len, hfp));
            }
            fclose(hfp);
        }
        FREE(cpath);
        FREE(hpath);
    }

    FREE(metadirn);
    return res;
}

LinkTable *LinkTable_disk_open(const char *dirn)
{
    char *metadirn = path_append(META_DIR, dirn);
    char *path = path_append(metadirn, ".LinkTable");
    FILE *fp = fopen(path, "r");

    if (!fp) {
        FREE(path);
        FREE(metadirn);
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
        FREE(metadirn);
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
        FREE(metadirn);
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
        FREE(metadirn);
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
        FREE(metadirn);
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
            FREE(metadirn);
            return NULL;
        }
    }
    if (fclose(fp)) {
        lprintf(error, "cannot close the file pointer, %s\n", strerror(errno));
    }

    FREE(path);

    char *cpath = path_append(metadirn, ".httpdirfs_content");
    char *hpath = path_append(metadirn, ".httpdirfs_header");
    size_t content_len = 0;
    size_t header_len = 0;
    char *cdata = NULL;
    char *hdata = NULL;

    FILE *cfp = fopen(cpath, "rb");
    if (cfp) {
        if (fseek(cfp, 0, SEEK_END) == 0) {
            long c_sz = ftell(cfp);
            if (c_sz >= 0 && fseek(cfp, 0, SEEK_SET) == 0) {
                cdata = CALLOC(1, (size_t)c_sz + 1);
                content_len = fread(cdata, 1, (size_t)c_sz, cfp);
                cdata[content_len] = '\0';
            }
        }
        fclose(cfp);
    }

    FILE *hfp = fopen(hpath, "rb");
    if (hfp) {
        if (fseek(hfp, 0, SEEK_END) == 0) {
            long h_sz = ftell(hfp);
            if (h_sz >= 0 && fseek(hfp, 0, SEEK_SET) == 0) {
                hdata = CALLOC(1, (size_t)h_sz + 1);
                header_len = fread(hdata, 1, (size_t)h_sz, hfp);
                hdata[header_len] = '\0';
            }
        }
        fclose(hfp);
    }

    LinkTable_add_diagnostics(linktbl, cdata, content_len, hdata, header_len);
    FREE(cdata);
    FREE(hdata);
    FREE(cpath);
    FREE(hpath);
    FREE(metadirn);
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
            new_table = LinkTable_new(tmp_link->f_url, tmp_link->parent_table);
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
                link->type = LINK_INVALID;
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
                        new_table
                            = LinkTable_new(linktbl->links[i]->f_url, linktbl);
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

static size_t write_download_full_callback(void *recv_data, size_t size,
                                           size_t nmemb, void *userp)
{
    TransferStruct *ts = (TransferStruct *)userp;
    size_t recv_size = size * nmemb;
    if (CONFIG.advanced_parsing_mode && CONFIG.max_html_size >= 0) {
        if (ts->curr_size + recv_size > (size_t)CONFIG.max_html_size) {
            lprintf(warning,
                    "HTML directory page download exceeded max_html_size (%ld "
                    "bytes), aborting transfer\n",
                    (long)CONFIG.max_html_size);
            return 0; /* Causes CURLE_WRITE_ERROR */
        }
    }
    return write_memory_callback(recv_data, size, nmemb, userp);
}

TransferStruct Link_download_full(Link *link, TransferStruct *header_out)
{
    char *url = link->f_url;
    CURL *curl = Link_to_curl(link);

    TransferStruct ts = {0};
    ts.type = DATA;
    ts.transferring = 1;

    TransferStruct header_local = {0};
    TransferStruct *header_ptr = header_out ? header_out : &header_local;
    header_ptr->curr_size = 0;
    header_ptr->data = NULL;
    header_ptr->type = DATA;

    CURLcode ret = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                                    write_download_full_callback);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&ts);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_PRIVATE, (void *)&ts);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)header_ptr);
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

        FREE(header_ptr->data);
        header_ptr->curr_size = 0;

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
            if (!header_out) {
                free(header_ptr->data);
            }
            curl_easy_cleanup(curl);
            return ts;
        }
    } while (HTTP_temp_failure(http_resp));

    ret = curl_easy_getinfo(curl, CURLINFO_FILETIME, &(link->time));
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    if (!header_out) {
        FREE(header_local.data);
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

    long res;
    if (link->is_virtual) {
        if (offset < 0 || (size_t)offset >= link->content_length
            || !link->virtual_content || req_size == 0) {
            res = 0;
        } else {
            size_t remaining = link->content_length - (size_t)offset;
            if (req_size > remaining) {
                req_size = remaining;
            }
            memcpy(output_buf, link->virtual_content + offset, req_size);
            res = (long)req_size;
        }
    } else {
        res = Link_download(link, output_buf, req_size, offset, NULL);
    }
    LinkTable_unref(link->parent_table);
    return res;
}

static int hex_char_to_val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

void make_link_relative(const char *page_url, char *link_url)
{
    if (!page_url || !link_url) {
        return;
    }

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

    /* Unescape the page path prefix so we can match against link_url
       regardless of whether spaces/special characters are percent-encoded
       or raw in either URL. */
    char *unescaped_url = curl_easy_unescape(NULL, page_url, 0, NULL);
    const char *url = unescaped_url ? unescaped_url : page_url;
    size_t url_len = strlen(url);

    const char *link_ptr = link_url;
    int matched = 1;
    for (size_t i = 0; i < url_len; i++) {
        unsigned char expected = (unsigned char)url[i];
        if (link_ptr[0] == '%' && isxdigit((unsigned char)link_ptr[1])
            && isxdigit((unsigned char)link_ptr[2])) {
            int hex_val = (hex_char_to_val(link_ptr[1]) << 4)
                          | hex_char_to_val(link_ptr[2]);
            if (hex_val == expected) {
                link_ptr += 3;
                continue;
            }
        }
        if ((unsigned char)link_ptr[0] == expected) {
            link_ptr += 1;
            continue;
        }
        matched = 0;
        break;
    }

    if (matched) {
        if (url_len > 0 && url[url_len - 1] != '/') {
            if (*link_ptr != '/') {
                matched = 0;
            } else {
                link_ptr++;
            }
        }
    }

    if (unescaped_url) {
        curl_free(unescaped_url);
    }

    if (!matched) {
        return;
    }

    /* Move the part of the link URL after the parent page's path to
       the beginning of the link URL string, discarding what came
       before it. */
    size_t skip_len = (size_t)(link_ptr - link_url);
    memmove(link_url, link_url + skip_len, strlen(link_url) - skip_len + 1);
}
