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

#ifndef MEMCACHE_H
#define MEMCACHE_H
/**
 * \file memcache.h
 * \brief Memory cache and data transfer definitions header
 */


#include <stddef.h>

typedef struct Link Link;
typedef struct Cache Cache;


/**
 * \brief specify the type of data transfer
 */
typedef enum { FILESTAT = 's', DATA = 'd' } TransferType;

/**
 * \brief For storing transfer data and metadata
 */
typedef struct TransferStruct {
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
    struct ActiveDownload *ad_ptr;
} TransferStruct;

/**
 * \brief Callback function for file transfer
 */
size_t write_memory_callback(void *recv_data, size_t size, size_t nmemb,
                             void *userp);

#endif
