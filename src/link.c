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
 * \brief Link structure and tree handling functions implementation
 */

#include "link.h"

#include "cache.h"
#include "config.h"
#include "link_parser.h"
#include "log.h"
#include "network.h"
#include "transfer.h"
#include "url.h"
#include "util.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
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

/**
 * \brief LinkTable generation priority lock
 * \details This allows LinkTable generation to be run exclusively. This
 * effectively gives LinkTable generation priority over file transfer.
 */
static pthread_mutex_t link_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * \brief create a new Link
 */
Link *Link_new(const char *linkname, LinkType type)
{
    Link *link = CALLOC(1, sizeof(Link));

    strncpy(link->linkname, linkname, NAME_MAX);
    strncpy(link->linkpath, linkname, NAME_MAX);

    link->type = type;
    link->parent_table = NULL;
    link->next_table = NULL;
    link->cache_ptr = NULL;
    link->time = 0;
    link->content_length = 0;
    link->is_virtual = 0;
    link->virtual_content = NULL;

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
     * Cache-first HEAD/stat resolution: resolve stats from disk container
     * without sending network requests if already cached.
     */
    if (CACHE_SYSTEM_INIT) {
        for (int i = 0; i < linktbl->size; i++) {
            Link *this_link = linktbl->links[i];
            if (this_link->type == LINK_UNINITIALISED_FILE
                || this_link->type == LINK_UNINITIALISED_DIR) {
                CacheStat cs;
                if (CacheContainer_read_head(this_link->f_url, &cs) == 1) {
                    this_link->time = cs.remote_mtime;
                    this_link->content_length = (size_t)cs.content_length;
                    this_link->type = cs.link_type;
                }
            }
        }
    }

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
    /*
     * --------------------- Enable cache system --------------------
     */
    if (CONFIG.cache_enabled) {
        CacheSystem_init(url);
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

int is_ancestor_head_link(const LinkTable *linktbl, const char *target_url)
{
    if (!linktbl || !target_url || target_url[0] == '\0') {
        return 0;
    }

    /* Check current folder's head link (prevent self-loops) */
    if (linktbl->links && linktbl->size > 0 && linktbl->links[0]
        && url_matches_head_link(target_url, linktbl->links[0]->f_url)) {
        return 1;
    }

    /* Check ancestor tables in parent_tbl chain */
    for (const LinkTable *cur = linktbl->parent_tbl; cur != NULL;
         cur = cur->parent_tbl) {
        if (cur->links && cur->size > 0 && cur->links[0]
            && url_matches_head_link(target_url, cur->links[0]->f_url)) {
            return 1;
        }
    }

    /* Check ROOT_LINK_TBL explicitly as safety fallback */
    if (ROOT_LINK_TBL && ROOT_LINK_TBL != linktbl && ROOT_LINK_TBL->links
        && ROOT_LINK_TBL->size > 0 && ROOT_LINK_TBL->links[0]
        && url_matches_head_link(target_url, ROOT_LINK_TBL->links[0]->f_url)) {
        return 1;
    }

    return 0;
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
        if (parent_link && parent_link->next_table == tbl) {
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
    linktbl->index_time = time(NULL);
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

LinkTable *LinkTable_new(const char *url, LinkTable *parent_tbl)
{
    LinkTable *linktbl = NULL;

    /*
     * Attempt to load the LinkTable from the disk. The unified single-file
     * cache stores the raw HTTP response (response headers + HTML payload)
     * of the directory listing; the LinkTable is regenerated in memory
     * on-the-fly with LinkTable_parse_html(), in well under a millisecond.
     */
    if (CACHE_SYSTEM_INIT) {
        char *payload = NULL;
        size_t payload_len = 0;
        char *http_header = NULL;
        size_t http_header_len = 0;
        time_t cache_time = 0;
        char *resolved_url = NULL;
        int loaded = CacheContainer_read_with_time(
            url, &payload, &payload_len, &http_header, &http_header_len,
            &cache_time, &resolved_url);
        if (loaded == 1) {
            /*
             * Relative links in the cached listing must resolve against the
             * effective URL the listing was actually served from, not the
             * originally requested URL (a redirect /dir -> /dir/ changes the
             * base path).
             */
            const char *eff_url
                = (resolved_url && resolved_url[0]) ? resolved_url : url;
            if (!CONFIG.allow_external_origin && strcmp(eff_url, url) != 0
                && is_cross_origin(url, eff_url)) {
                /*
                 * A fresh cross-origin redirect is rejected by the transfer
                 * layer when --allow-external-origin is disabled, so a cached
                 * listing served from an external origin (cached while the
                 * option was enabled) must not be parsed: the parser would
                 * compare its links against the original URL and drop them.
                 * Invalidate the entry and refetch, letting the current
                 * policy apply to the fresh redirect.
                 */
                lprintf(warning,
                        "cached redirect %s -> %s is blocked by the current "
                        "origin policy; invalidating and refetching\n",
                        url, eff_url);
                CacheContainer_delete(url);
                CacheContainer_delete(eff_url);
                FREE(payload);
                FREE(http_header);
                FREE(resolved_url);
                payload = NULL;
            } else {
                /*
                 * Re-apply the max_html_size gate to a cached body. A cached
                 * listing for a promoted (no-trailing-slash) directory that now
                 * exceeds max_html_size (e.g. the limit was lowered after it
                 * was cached) is treated as an empty folder, not parsed. Real
                 * directories (trailing slash) are exempt, as at download time.
                 */
                const char *qf = strpbrk(eff_url, "?#");
                size_t tlen = qf ? (size_t)(qf - eff_url) : strlen(eff_url);
                int real_dir = (tlen > 0 && eff_url[tlen - 1] == '/');
                if (!real_dir && CONFIG.website_mode && CONFIG.max_html_size > 0
                    && (off_t)payload_len > CONFIG.max_html_size) {
                    lprintf(warning,
                            "cached listing for %s is %zu bytes, exceeding "
                            "max_html_size (%ld bytes); leaving it as an empty "
                            "folder\n",
                            url, payload_len, (long)CONFIG.max_html_size);
                    FREE(payload);
                    FREE(http_header);
                    linktbl = LinkTable_alloc(url);
                    linktbl->parent_tbl = parent_tbl;
                    linktbl->index_time = time(NULL);
                    CacheContainer_delete(url);
                    if (strcmp(eff_url, url) != 0) {
                        CacheContainer_delete(eff_url);
                    }
                    FREE(resolved_url);
                    return linktbl;
                }
                lprintf(info,
                        "loaded cached directory listing for %s in < 1 ms\n",
                        url);
                linktbl = LinkTable_alloc(url);
                linktbl->parent_tbl = parent_tbl;
                linktbl->index_time = cache_time ? cache_time : time(NULL);
                LinkTable_parse_html(linktbl, eff_url, payload);
                LinkTable_fill(linktbl);
                LinkTable_add_diagnostics(linktbl, payload, payload_len,
                                          http_header, http_header_len);
                FREE(payload);
                FREE(http_header);
                FREE(resolved_url);
            }
        } else if (loaded == -1) {
            lprintf(error,
                    "Failed to read the cached directory listing "
                    "for %s!\n",
                    url);
            FREE(resolved_url);
        } else {
            lprintf(info, "cached directory listing not found for %s\n", url);
            FREE(resolved_url);
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

        if (ts.failed) {
            /*
             * The listing could not be fetched at all (non-200, non-temporary
             * response). Mark the table as failed (index_time == 0) so the
             * callers do not cache it or attach it to the link; the next
             * access retries the download instead of serving a stale-looking
             * empty folder for the whole refresh interval.
             */
            lprintf(warning,
                    "failed to download directory listing for %s; "
                    "will retry on next access\n",
                    url);
            FREE(ts.data);
            FREE(header_ts.data);
            FREE(ts.eff_url);
            linktbl->index_time = 0;
            return linktbl;
        }

        /*
         * A directory is never turned into a file. If the listing arrived as
         * an empty body or exceeded max_html_size (the capped download
         * aborted), keep the freshly allocated table as an empty folder (head
         * link only, no children) rather than parsing a partial body. The
         * caller attaches it to the link, so the entry stays a directory.
         */
        if (ts.curr_size == 0 || ts.cap_hit) {
            lprintf(warning,
                    ts.cap_hit
                        ? "directory listing for %s exceeds max_html_size "
                          "(%ld bytes); leaving it as an empty folder\n"
                        : "failed to download directory listing for %s; "
                          "leaving it as an empty folder\n",
                    url, (long)CONFIG.max_html_size);
            FREE(ts.data);
            FREE(header_ts.data);
            FREE(ts.eff_url);
            return linktbl;
        }

        /*
         * Relative links must resolve against the effective URL the listing
         * was served from, not the originally requested URL.
         */
        const char *parse_url
            = (ts.eff_url && ts.eff_url[0]) ? ts.eff_url : url;

        /*
         * Otherwise parsed the received data
         */
        LinkTable_parse_html(linktbl, parse_url, ts.data);

        LinkTable_fill(linktbl);

        LinkTable_add_diagnostics(linktbl, ts.data, ts.curr_size,
                                  header_ts.data, header_ts.curr_size);

        /*
         * Save the raw HTTP response (headers + HTML payload) to the
         * unified single-file container cache, keyed by the effective URL.
         * The redirect container written by Link_download_full() then maps
         * the requested URL to this payload on cache reads.
         */
        if (CACHE_SYSTEM_INIT
            && CacheContainer_write(parse_url, ts.data, ts.curr_size,
                                    header_ts.data, header_ts.curr_size)) {
            lprintf(error, "Failed to save the directory listing container "
                           "file!\n");
        }

        FREE(ts.data);
        FREE(header_ts.data);
        FREE(ts.eff_url);
    }

    LinkTable_print(linktbl);
    return linktbl;
}

static int is_table_expired(const LinkTable *tbl)
{
    if (!tbl || tbl->index_time <= 0 || CONFIG.refresh_timeout < 0) {
        return 0;
    }
    if (tbl->parent_link && tbl->parent_link->is_virtual) {
        return 0;
    }
    int64_t age = (int64_t)time(NULL) - tbl->index_time;
    return age > CONFIG.refresh_timeout;
}

static void retire_expired_table(LinkTable *tbl)
{
    if (!tbl) {
        return;
    }
    Link *parent_link = tbl->parent_link;
    if (parent_link && parent_link->next_table == tbl) {
        parent_link->next_table = NULL;
    }
    /*
     * Detached: never touch the parent link's slot again, otherwise a later
     * LinkTable_unref() on the retired table could detach a replacement
     * table that the parent has meanwhile attached.
     */
    tbl->parent_link = NULL;
    tbl->orphaned = 1;
    tbl->refcount++;
    PTHREAD_MUTEX_UNLOCK(&link_lock);
    LinkTable_unref(tbl);
    PTHREAD_MUTEX_LOCK(&link_lock);
}

static pthread_mutex_t root_refresh_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * A root table must be (re)loaded when it has gone stale, or when it was never
 * successfully populated (index_time left at 0 by a failed initial download in
 * LinkSystem_init). The latter lets a mount whose root listing failed at
 * startup be retried once the server recovers, instead of remaining an empty
 * folder for the lifetime of the mount. A failed refresh still never replaces
 * an already-populated root table (handled by the caller below).
 */
static int root_table_needs_reload(const LinkTable *tbl)
{
    return is_table_expired(tbl) || (tbl && tbl->index_time <= 0);
}

static void check_and_refresh_root_table(void)
{
    PTHREAD_MUTEX_LOCK(&link_lock);
    if (!ROOT_LINK_TBL || ROOT_LINK_TBL->size == 0 || !ROOT_LINK_TBL->links
        || !ROOT_LINK_TBL->links[0]
        || !root_table_needs_reload(ROOT_LINK_TBL)) {
        PTHREAD_MUTEX_UNLOCK(&link_lock);
        return;
    }

    char root_url[PATH_MAX];
    strncpy(root_url, ROOT_LINK_TBL->links[0]->f_url, PATH_MAX);
    root_url[PATH_MAX - 1] = '\0';
    PTHREAD_MUTEX_UNLOCK(&link_lock);

    PTHREAD_MUTEX_LOCK(&root_refresh_lock);

    PTHREAD_MUTEX_LOCK(&link_lock);
    if (!ROOT_LINK_TBL || !root_table_needs_reload(ROOT_LINK_TBL)) {
        PTHREAD_MUTEX_UNLOCK(&link_lock);
        PTHREAD_MUTEX_UNLOCK(&root_refresh_lock);
        return;
    }
    PTHREAD_MUTEX_UNLOCK(&link_lock);

    LinkTable *new_root = NULL;
    if (CONFIG.mode == NORMAL) {
        new_root = LinkTable_new(root_url, NULL);
    } else if (CONFIG.mode == SINGLE) {
        new_root = single_LinkTable_new(root_url);
    } else if (CONFIG.mode == SONIC) {
        if (!CONFIG.sonic_id3) {
            new_root = sonic_LinkTable_new_index("0");
        } else {
            new_root = sonic_LinkTable_new_id3(0, "0");
        }
    }

    if (new_root && new_root->index_time <= 0) {
        /*
         * The root refresh failed; keep the previous root table so the
         * namespace is not hidden for the whole refresh interval.
         */
        lprintf(warning,
                "failed to refresh root listing for %s; keeping "
                "the previous table\n",
                root_url);
        LinkTable_free(new_root);
        new_root = NULL;
    }

    PTHREAD_MUTEX_LOCK(&link_lock);
    if (new_root) {
        LinkTable *old_root = ROOT_LINK_TBL;
        ROOT_LINK_TBL = new_root;
        if (old_root) {
            old_root->orphaned = 1;
            if (old_root->refcount == 0) {
                LinkTable_free(old_root);
            }
        }
    } else if (ROOT_LINK_TBL) {
        time_t delay = (CONFIG.http_wait_sec > 0) ? CONFIG.http_wait_sec : 5;
        ROOT_LINK_TBL->index_time = time(NULL) - CONFIG.refresh_timeout + delay;
    }
    PTHREAD_MUTEX_UNLOCK(&link_lock);
    PTHREAD_MUTEX_UNLOCK(&root_refresh_lock);
}

LinkTable *path_to_LinkTable(const char *path)
{
    Link *link = NULL;
    Link *tmp_link = NULL;
    LinkTable *next_table = NULL;

    if (!strcmp(path, "/")) {
        check_and_refresh_root_table();
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
            if (is_table_expired(next_table)) {
                if (link->parent_table) {
                    link->parent_table->refcount++;
                }
                retire_expired_table(next_table);
                if (link->parent_table) {
                    link->parent_table->refcount--;
                }
                next_table = NULL;
            } else {
                next_table->refcount++;
                next_table->orphaned = 0;
            }
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

        if (new_table->index_time <= 0) {
            /*
             * The listing download failed; do not attach the failed table.
             * The entry stays without a table, so the next access retries
             * the download.
             */
            lprintf(warning,
                    "failed to load directory listing for %s; "
                    "will retry on next access\n",
                    tmp_link->f_url);
            LinkTable_free(new_table);
            LinkTable_unref(link->parent_table);
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
                if (next_table && is_table_expired(next_table)) {
                    linktbl->refcount++;
                    retire_expired_table(next_table);
                    linktbl->refcount--;
                    next_table = NULL;
                }
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
                            if (parent_link
                                && parent_link->next_table == linktbl) {
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

                    if (new_table->index_time <= 0) {
                        /*
                         * The listing download failed; do not attach the
                         * failed table, so the next access retries the
                         * download.
                         */
                        PTHREAD_MUTEX_LOCK(&link_lock);
                        linktbl->refcount--;
                        PTHREAD_MUTEX_UNLOCK(&link_lock);
                        LinkTable_free(new_table);
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
    check_and_refresh_root_table();

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
