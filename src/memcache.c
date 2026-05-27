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
 * \file memcache.c
 * \brief Memory cache and data transfer definitions implementation
 */

#include "memcache.h"

#include "cache.h"
#include "log.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

size_t write_memory_callback(void *recv_data, size_t size, size_t nmemb,
                             void *userp)
{
    TransferStruct *ts = (TransferStruct *)userp;

    if (size != 0 && nmemb > (SIZE_MAX - ts->curr_size - 1) / size) {
        lprintf(fatal, "Response buffer size overflow!\n");
    }
    size_t recv_size = size * nmemb;

    if (ts->cache_ptr) {
        PTHREAD_MUTEX_LOCK(&ts->cache_ptr->dl_lock);
    }

    void *new_data = REALLOC(ts->data, ts->curr_size + recv_size + 1);
    ts->data = new_data;

    memmove(&ts->data[ts->curr_size], recv_data, recv_size);
    ts->curr_size += recv_size;
    ts->data[ts->curr_size] = '\0';

    if (ts->cache_ptr) {
        if (ts->ad_ptr) {
            PTHREAD_COND_BROADCAST(&ts->ad_ptr->cond);
        }
        PTHREAD_MUTEX_UNLOCK(&ts->cache_ptr->dl_lock);
    }

    return recv_size;
}
