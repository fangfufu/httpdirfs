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

#ifndef TRANSFER_H
#define TRANSFER_H

/**
 * \file transfer.h
 * \brief Data transfer, curl handles, and download routines header
 */

#include <curl/curl.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct Link Link;
typedef struct Cache Cache;
typedef struct ActiveDownload ActiveDownload;
typedef struct TransferStruct TransferStruct;

/**
 * \brief specify the type of data transfer
 */
typedef enum { FILESTAT = 's', DATA = 'd' } TransferType;

/**
 * \brief Callback for asynchronous transfer completion
 */
typedef void (*TransferCompleteCb)(TransferStruct *ts, CURL *curl,
                                   CURLcode result, const char *url);

/**
 * \brief For storing transfer data and metadata
 */
struct TransferStruct {
    /** \brief The array to store the data */
    char *data;
    /** \brief The current size of the array */
    size_t curr_size;
    /** \brief The type of transfer being done */
    TransferType type;
    /** \brief Whether transfer is in progress */
    volatile int transferring;
    /** \brief The link associated with the transfer */
    Link *link;
    /** \brief The Cache structure associated with the transfer */
    Cache *cache_ptr;
    /** \brief The ActiveDownload structure associated with the transfer */
    ActiveDownload *ad_ptr;
    /** \brief Optional completion callback for asynchronous transfers */
    TransferCompleteCb on_complete;
    /** \brief Abort the transfer once curr_size exceeds this (0 = no cap) */
    size_t size_cap;
    /** \brief Set to 1 by the capped callback if the transfer was aborted */
    int cap_hit;
};

#include "link.h"

/**
 * \brief Callback function for file transfer
 */
size_t write_memory_callback(void *recv_data, size_t size, size_t nmemb,
                             void *userp);

/**
 * \brief Body callback for capped full downloads
 * \details Buffers the body like write_memory_callback but aborts the transfer
 * (returning 0, setting ts->cap_hit) once the accumulated size would exceed
 * ts->size_cap.
 */
size_t write_memory_capped_callback(void *recv_data, size_t size, size_t nmemb,
                                    void *userp);

/**
 * \brief Configure a CURL easy handle for a Link
 */
CURL *Link_to_curl(Link *link);

/**
 * \brief Asynchronously request file stat for a Link via HEAD request
 */
void Link_req_file_stat(Link *this_link);

/**
 * \brief Set the stats of a link after curl transfer finishes
 */
void Link_set_file_stat(Link *this_link, CURL *curl);

/**
 * \brief Pure classification logic for HTTP response in link stat resolution
 */
LinkType Link_classify_response(LinkType current_type, long http_resp,
                                curl_off_t cl, const char *content_type,
                                size_t *content_len_out);

/**
 * \brief Download a link's content to memory
 * \warning You MUST free the data field in TransferStruct after use!
 */
TransferStruct Link_download_full(Link *head_link, TransferStruct *header_out);

/**
 * \brief Download a Link range into buffer
 * \return the number of bytes downloaded
 */
long Link_download(Link *link, char *output_buf, size_t req_size, off_t offset,
                   Cache *cf);

/**
 * \brief Download a path directly (without cache)
 * \return the number of bytes downloaded
 */
long path_download(const char *path, char *output_buf, size_t size,
                   off_t offset);

#endif
