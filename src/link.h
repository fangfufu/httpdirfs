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

#ifndef LINK_H
#define LINK_H

/**
 * \file link.h
 * \brief Link structure and handling functions header
 */

#include <limits.h>
#include <sys/types.h>

#include "link_parser.h"
#include "sonic.h"
#include "url.h"

typedef struct Cache Cache;
typedef struct Link Link;
typedef struct LinkTable LinkTable;
struct TransferStruct;

/**
 * \brief the link type
 */
typedef enum {
    LINK_HEAD = 'H',
    LINK_DIR = 'D',
    LINK_FILE = 'F',
    LINK_INVALID = 'I',
    LINK_UNINITIALISED_FILE = 'U',
    LINK_UNINITIALISED_DIR = 'V',
} LinkType;

/**
 * \brief link table type
 * \details index 0 contains the Link for the base URL
 */
struct LinkTable {
    int size;
    time_t index_time;
    Link **links;
    int refcount;
    int orphaned;
    struct LinkTable *parent_tbl;
    Link *parent_link;
};

/**
 * \brief Link type data structure
 */
struct Link {
    /** \brief The parent LinkTable of this link */
    struct LinkTable *parent_table;
    /** \brief The link name in the last level of the URL */
    char linkname[NAME_MAX + 1];
    /** \brief This is for storing the unescaped path */
    char linkpath[NAME_MAX + 1];
    /** \brief The full URL of the file */
    char f_url[PATH_MAX + 1];
    /** \brief The type of the link */
    LinkType type;
    /** \brief CURLINFO_CONTENT_LENGTH_DOWNLOAD of the file */
    size_t content_length;
    /** \brief The next LinkTable level, if it is a LINK_DIR */
    LinkTable *next_table;
    /** \brief CURLINFO_FILETIME obtained from the server */
    long time;
    /** \brief The pointer associated with the cache file */
    Cache *cache_ptr;
    /** \brief Stores *sonic related data */
    Sonic sonic;
    /** \brief In-memory virtual content for diagnostic files */
    char *virtual_content;
    /** \brief Whether this link is a virtual link (not backed by network) */
    int is_virtual;
    /** \brief Whether this directory link is hidden from readdir output
     *  because its file count is not known yet (background preloading)
     */
    int hidden;
};

/**
 * \brief root link table
 */
extern LinkTable *ROOT_LINK_TBL;

/**
 * \brief create a new Link
 */
Link *Link_new(const char *linkname, LinkType type);


/**
 * \brief initialise link sub-system.
 */
LinkTable *LinkSystem_init(const char *raw_url);

/**
 * \brief create a new LinkTable
 */
LinkTable *LinkTable_new(const char *url, LinkTable *parent_tbl);

/**
 * \brief begin building a NORMAL-mode listing table for url
 * \details Allocates the table (fresh index_time, head link) and links it
 * to its parent; the listing download runs under the table's head link.
 * Shared by the synchronous path (LinkTable_new) and the asynchronous
 * preload path.
 */
LinkTable *LinkTable_begin_listing(const char *url, LinkTable *parent_tbl);

/**
 * \brief apply a downloaded listing body to a begun table
 * \details The synchronous and asynchronous download paths funnel through
 * here. A failed fetch (non-200, non-temporary) marks the table as failed
 * (index_time == 0) so callers do not cache or attach it; an empty body or
 * a capped download keeps the table as an empty folder; otherwise the body
 * is parsed, the table is filled, and the raw response is saved to the
 * container cache. Consumes the transfer: all of ts's and header's buffers
 * (including ts->eff_url) are freed.
 */
void LinkTable_finish_listing(LinkTable *linktbl, const char *url,
                              struct TransferStruct *ts,
                              struct TransferStruct *header);

/**
 * \brief find the link associated with a path
 */
Link *path_to_Link(const char *path);

/**
 * \brief return the link table for the associated path
 */
LinkTable *path_to_LinkTable(const char *path);

/**
 * \brief Allocate a LinkTable
 * \note This does not fill in the LinkTable.
 */
LinkTable *LinkTable_alloc(const char *url);

/**
 * \brief free a LinkTable
 */
void LinkTable_free(LinkTable *linktbl);

/**
 * \brief increment the reference count of a LinkTable
 */
void LinkTable_ref(LinkTable *tbl);

/**
 * \brief decrement the reference count of a LinkTable
 */
void LinkTable_unref(LinkTable *tbl);

/**
 * \brief mark a LinkTable as orphaned so it can be evicted
 */
void LinkTable_mark_orphaned(LinkTable *tbl);

/**
 * \brief whether a link should be reported by readdir
 * \details a link is listed unless it is LINK_INVALID or hidden because its
 * directory listing has not been preloaded yet
 */
int Link_should_list(const Link *link);

/**
 * \brief download a directory listing and attach it to its link
 * \details shared by client code and the background preload worker. If the
 * link already has a live (non-expired) table it is returned as-is.
 * Otherwise a fresh table is downloaded (mode-appropriate loader) and
 * attached, racing safely with concurrent loaders.
 * \param link the directory link to load
 * \param created_new out parameter; set to 1 iff a fresh table was attached
 * by this call, 0 if an existing table was returned. May be NULL.
 * \return the table carrying exactly one reference for the caller, or NULL
 * if the download failed (a failed table is never attached). On the rare
 * allocation failure the link is marked LINK_INVALID.
 */
LinkTable *LinkTable_load_and_attach(Link *link, int *created_new);

/**
 * \brief attach a fully loaded table to its link (async preload path)
 * \details Attaches exactly like the tail of LinkTable_load_and_attach(),
 * racing safely with concurrent on-demand loaders. A failed table
 * (index_time == 0) is freed and the entry left without a table so the
 * next access retries the download.
 * \return the table carrying exactly one reference for the caller, or NULL
 * (failed table, already freed).
 */
LinkTable *LinkTable_attach_loaded(Link *link, LinkTable *new_table);

/**
 * \brief schedule background preloading of a freshly loaded table's
 * directory children
 * \details guarded no-op unless progressive preloading is enabled, the mode
 * is NORMAL, and the table loaded successfully. Otherwise each plain
 * (non-virtual) LINK_DIR child is hidden and enqueued, with one
 * queue-lifetime reference on the table per enqueued link.
 */
void Link_preload_directories(LinkTable *tbl);

/**
 * \brief print a LinkTable
 */
void LinkTable_print(LinkTable *linktbl);

/**
 * \brief add a Link to a LinkTable
 */
void LinkTable_add(LinkTable *linktbl, Link *link);

/**
 * \brief Attach a .httpdirfs diagnostics directory to a LinkTable
 */
void LinkTable_add_diagnostics(LinkTable *linktbl, const char *content,
                               size_t content_len, const char *header,
                               size_t header_len);

/**
 * \brief Check if target_url matches the head link of the current table
 * or any ancestor LinkTable in its parent chain up to root.
 * \return 1 if target_url matches an ancestor head link, 0 otherwise.
 */
int is_ancestor_head_link(const LinkTable *linktbl, const char *target_url);

#endif
