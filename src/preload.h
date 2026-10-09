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

#ifndef PRELOAD_H
#define PRELOAD_H

/**
 * \file preload.h
 * \brief Background directory listing preloader subsystem header
 */

typedef struct Link Link;

/**
 * \brief enqueue a directory link for background listing preload
 * \details The preload worker will download the link's directory listing
 * and attach it to the link. The caller must hold one extra reference on
 * link->parent_table (the "queue-lifetime reference") for the duration of
 * the queue entry; the worker releases exactly one such reference after
 * processing the item.
 * \return 0 on success, -1 on allocation failure (the item was not queued)
 */
int Preload_enqueue(Link *link);

/**
 * \brief spawn the preload worker
 * \details A single worker thread is started exactly once; it drains the
 * queue and drives the fetches asynchronously through the shared curl
 * multi handle. It must be called from the process that will serve the
 * mount: in FUSE daemon (background) mode that is the daemon child, which
 * is why the call is made from the FUSE init callback rather than from
 * main(). Items enqueued before the worker starts are picked up once it
 * does. If the worker cannot be spawned, every queued link is unhidden so
 * the entries degrade to on-demand loading.
 */
void Preload_start(void);

/**
 * \brief return the number of items currently waiting in the queue
 * \details for diagnostics and testing
 */
int Preload_queue_depth(void);

#endif
