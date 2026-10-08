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
 * \file preload.c
 * \brief Background directory listing preloader: a FIFO queue of directory
 * links drained by a single worker thread that drives the fetches
 * asynchronously through the shared curl multi handle. The transfer state
 * machine (redirects, temporary-failure retries, cap handling) runs in the
 * completion callback; the worker pumps the multi handle, requeues due
 * retries, and finally parses and attaches each finished listing.
 * Workers never schedule further preloads, so background work is bounded to
 * one level ahead of whatever the client code loaded.
 */

#include "preload.h"

#include "cache.h"
#include "config.h"
#include "link.h"
#include "log.h"
#include "network.h"
#include "transfer.h"
#include "util.h"

#include <pthread.h>
#include <stdlib.h>
#include <time.h>

typedef struct PreloadItem {
    Link *link;
    struct PreloadItem *next;
} PreloadItem;

/*
 * One in-flight asynchronous listing fetch. Owned by the preload worker
 * thread; the completion callback runs on whichever thread pumps the shared
 * multi handle, under the transfer lock, and only performs bookkeeping here.
 */
typedef struct PreloadFetch {
    Link *link;
    /** \brief the table being built; the fetch runs under its head link */
    LinkTable *table;
    /** \brief NULL once the completion callback has cleaned the handle up */
    CURL *curl;
    TransferStruct ts;
    TransferStruct header;
    /** \brief nonzero: temporary HTTP failure, requeue at this time */
    time_t retry_at;
    /** \brief terminal state reached (success or failure) */
    int finished;
    struct PreloadFetch *next;
} PreloadFetch;

static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t q_not_empty = PTHREAD_COND_INITIALIZER;
static PreloadItem *q_head = NULL;
static PreloadItem *q_tail = NULL;
static int q_depth = 0;
static int stopping = 0;
static pthread_t worker;
static int worker_started = 0;
/** \brief the batch currently in flight; touched by the worker only */
static PreloadFetch *inflight = NULL;

static void finish_fetch_failed(PreloadFetch *f)
{
    if (f->curl) {
        curl_easy_cleanup(f->curl);
        f->curl = NULL;
    }
    f->finished = 1;
}

/*
 * Completion callback for a preload fetch. Runs under the transfer lock on
 * whichever thread pumped the multi handle, so it only does bookkeeping and
 * (lock-free) requeueing, never blocking calls. Mirrors the synchronous
 * state machine in Link_download_full().
 */
static void preload_on_complete(TransferStruct *ts, CURL *curl, CURLcode result,
                                const char *url)
{
    (void)url;
    PreloadFetch *f = ts->user_data;
    if (!f) {
        return;
    }
    Link *link = f->link;

    if (result && !ts->cap_hit) {
        /*
         * Transport failure. The capped abort is not a failure: the 200
         * response is final and the (partial) body is treated as an empty
         * folder by LinkTable_finish_listing().
         */
        lprintf(error, "%d - %s <%s>\n", result, curl_easy_strerror(result),
                link->f_url);
        /*
         * A transport failure is not an empty listing: mark the table as
         * failed so it is neither cached nor attached (the synchronous path
         * reaches the same state through the http_resp != HTTP_OK branch).
         */
        ts->failed = 1;
        finish_fetch_failed(f);
        return;
    }

    long http_resp = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);

    /*
     * Redirects are followed one hop at a time (FOLLOWLOCATION is off).
     * Each hop re-checks the target against the mounted origin before the
     * custom headers / credentials are sent, so a cross-origin redirect
     * never receives them.
     */
    int redir = Transfer_follow_redirect(curl, origin_base_url(link));
    if (redir < 0) {
        ts->failed = 1;
        finish_fetch_failed(f);
        return;
    }
    if (redir) {
        ts->redirects++;
        if (ts->redirects > MAX_REDIRECTS) {
            lprintf(error, "too many redirects for %s\n", link->f_url);
            ts->failed = 1;
            finish_fetch_failed(f);
            return;
        }
        /*
         * Reset the accumulated buffers so only the final hop's data and
         * headers remain. on_complete runs while the transfer lock is held,
         * so use the lock-free requeue.
         */
        FREE(ts->data);
        ts->curr_size = 0;
        ts->cap_hit = 0;
        FREE(f->header.data);
        f->header.curr_size = 0;
        ts->transferring = 1;
        if (transfer_requeue_locked(curl) != 0) {
            lprintf(error, "failed to requeue the preload of %s\n",
                    link->f_url);
            ts->transferring = 0;
            ts->failed = 1;
            finish_fetch_failed(f);
            return;
        }
        return;
    }

    if (HTTP_temp_failure((HTTPResponseCode)http_resp)) {
        lprintf(warning, "URL: %s, HTTP %ld, retrying later.\n", link->f_url,
                http_resp);
        FREE(ts->data);
        ts->curr_size = 0;
        ts->cap_hit = 0;
        FREE(f->header.data);
        f->header.curr_size = 0;
        f->retry_at = time(NULL) + CONFIG.http_wait_sec;
        return; /* the worker requeues the handle when the deadline comes due */
    }

    if (http_resp != HTTP_OK) {
        lprintf(warning, "cannot retrieve URL: %s, HTTP %ld\n", link->f_url,
                http_resp);
        ts->failed = 1;
        finish_fetch_failed(f);
        return;
    }

    /*
     * Success: extract the metadata before cleaning up the handle,
     * mirroring the tail of the synchronous path.
     */
    curl_easy_getinfo(curl, CURLINFO_FILETIME, &(f->table->links[0]->time));
    char *eff_url = NULL;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff_url);
    if (CACHE_SYSTEM_INIT && eff_url && eff_url[0]
        && strcmp(eff_url, link->f_url) != 0) {
        CacheContainer_write_redirect(link->f_url, eff_url, http_resp);
    }
    if (eff_url && eff_url[0]) {
        ts->eff_url = STRDUP(eff_url);
    }
    curl_easy_cleanup(curl);
    f->curl = NULL;
    f->finished = 1;
}

/*
 * Begin the asynchronous fetch of one queued link. On failure the link is
 * unhidden so the entry degrades to on-demand loading.
 */
static void start_fetch(Link *link)
{
    PreloadFetch *f = CALLOC(1, sizeof(PreloadFetch));
    LinkTable *parent = link->parent_table;
    if (!f) {
        lprintf(error, "failed to allocate a preload fetch for %s\n",
                link->f_url);
        goto unhide;
    }
    f->link = link;
    f->table = LinkTable_begin_listing(link->f_url, link->parent_table);
    f->ts.type = DATA;
    f->ts.transferring = 1;
    f->ts.on_complete = preload_on_complete;
    f->ts.user_data = f;
    f->curl = Link_setup_full_download(f->table->links[0], &f->ts, &f->header);
    if (!f->curl) {
        lprintf(error, "failed to set up the preload download for %s\n",
                link->f_url);
        LinkTable_free(f->table);
        FREE(f);
        goto unhide;
    }
    if (transfer_nonblocking(f->curl) != 0) {
        lprintf(error, "failed to queue the preload download for %s\n",
                link->f_url);
        curl_easy_cleanup(f->curl);
        LinkTable_free(f->table);
        FREE(f);
        goto unhide;
    }
    f->next = inflight;
    inflight = f;
    return;
unhide:
    link->hidden = 0;
    LinkTable_unref(parent);
}

static int all_fetches_finished(void)
{
    for (PreloadFetch *f = inflight; f; f = f->next) {
        if (!f->finished) {
            return 0;
        }
    }
    return 1;
}

/*
 * Re-add handles whose temporary-failure retry deadline has come due.
 * During shutdown pending retries are aborted instead of waited out so the
 * process can exit.
 */
static void requeue_due_retries(void)
{
    time_t now = time(NULL);
    for (PreloadFetch *f = inflight; f; f = f->next) {
        if (f->finished || !f->retry_at) {
            continue;
        }
        if (stopping) {
            f->retry_at = 0;
            f->ts.failed = 1;
            f->finished = 1;
            continue;
        }
        if (now >= f->retry_at) {
            f->retry_at = 0;
            f->ts.transferring = 1;
            if (transfer_nonblocking(f->curl) != 0) {
                lprintf(error, "failed to requeue the preload of %s\n",
                        f->link->f_url);
                f->ts.failed = 1;
                f->finished = 1;
            }
        }
    }
}

/*
 * Parse and attach a finished fetch, then release everything.
 * link->hidden and the queue-lifetime reference are released last, after the
 * link has been written to.
 */
static void finalize_fetch(PreloadFetch *f)
{
    Link *link = f->link;
    LinkTable *parent = link->parent_table;

    LinkTable_finish_listing(f->table, link->f_url, &f->ts, &f->header);
    if (f->table->index_time > 0) {
        LinkTable *attached = LinkTable_attach_loaded(link, f->table);
        if (attached) {
            LinkTable_unref(attached);
        }
    } else {
        lprintf(warning,
                "failed to preload directory listing for %s; "
                "showing it anyway\n",
                link->f_url);
        LinkTable_free(f->table);
    }
    link->hidden = 0;
    LinkTable_unref(parent);
    if (f->curl) {
        curl_easy_cleanup(f->curl);
    }
    FREE(f);
}

static void *preload_worker(void *arg)
{
    (void)arg;
    for (;;) {
        PTHREAD_MUTEX_LOCK(&q_lock);
        while (q_head == NULL && !stopping) {
            PTHREAD_COND_WAIT(&q_not_empty, &q_lock);
        }
        if (q_head == NULL) {
            /*
             * Queue empty and we are stopping: exit. The loop only
             * re-enters while work remains, so every in-flight fetch was
             * finalized before the last batch finished.
             */
            PTHREAD_MUTEX_UNLOCK(&q_lock);
            return NULL;
        }
        /*
         * Pop the whole queue as one batch so every fetch in it runs
         * concurrently on the shared multi handle.
         */
        PreloadItem *batch = q_head;
        q_head = NULL;
        q_tail = NULL;
        q_depth = 0;
        PTHREAD_MUTEX_UNLOCK(&q_lock);

        PreloadItem *item = batch;
        while (item) {
            PreloadItem *next = item->next;
            start_fetch(item->link);
            FREE(item);
            item = next;
        }

        /*
         * Pump until every fetch in the batch reaches a terminal state.
         * curl_multi_perform_once() blocks up to 100 ms waiting for
         * I/O, so this is not a busy loop; completion callbacks may run on
         * any thread that pumps the shared multi handle.
         */
        while (!all_fetches_finished()) {
            curl_multi_perform_once();
            requeue_due_retries();
        }

        PreloadFetch *f = inflight;
        inflight = NULL;
        while (f) {
            PreloadFetch *next = f->next;
            finalize_fetch(f);
            f = next;
        }
    }
}

static void preload_shutdown(void)
{
    PTHREAD_MUTEX_LOCK(&q_lock);
    if (!worker_started) {
        /*
         * No worker was ever spawned; nothing to join. Free any queued
         * items (their queue-lifetime references die with the process).
         */
        while (q_head) {
            PreloadItem *item = q_head;
            q_head = item->next;
            FREE(item);
        }
        q_tail = NULL;
        q_depth = 0;
        PTHREAD_MUTEX_UNLOCK(&q_lock);
        return;
    }
    stopping = 1;
    PTHREAD_COND_BROADCAST(&q_not_empty);
    PTHREAD_MUTEX_UNLOCK(&q_lock);

    /*
     * The worker finalizes its in-flight batch and drains the queue before
     * exiting, so after the join the queue is empty.
     */
    pthread_join(worker, NULL);
    worker_started = 0;
}

static int worker_start_locked(void)
{
    if (worker_started) {
        return 1;
    }
    if (pthread_create(&worker, NULL, preload_worker, NULL) != 0) {
        lprintf(error, "failed to spawn the preload worker; "
                       "directory preloading is disabled\n");
        return 0;
    }
    worker_started = 1;
    atexit(preload_shutdown);
    return 1;
}

int Preload_enqueue(Link *link)
{
    if (!link) {
        return -1;
    }
    PreloadItem *item = CALLOC(1, sizeof(PreloadItem));
    if (!item) {
        lprintf(error, "failed to allocate a preload queue item for %s\n",
                link->f_url);
        return -1;
    }
    item->link = link;

    PTHREAD_MUTEX_LOCK(&q_lock);
    if (q_tail) {
        q_tail->next = item;
    } else {
        q_head = item;
    }
    q_tail = item;
    q_depth++;
    PTHREAD_COND_BROADCAST(&q_not_empty);
    PTHREAD_MUTEX_UNLOCK(&q_lock);
    return 0;
}

void Preload_start(void)
{
    PTHREAD_MUTEX_LOCK(&q_lock);
    if (!worker_start_locked()) {
        /*
         * No worker could be spawned: unhide everything already queued so
         * the entries degrade to on-demand loading instead of staying
         * hidden forever.
         */
        PreloadItem *item = q_head;
        q_head = NULL;
        q_tail = NULL;
        q_depth = 0;
        PTHREAD_MUTEX_UNLOCK(&q_lock);
        while (item) {
            PreloadItem *next = item->next;
            item->link->hidden = 0;
            LinkTable_unref(item->link->parent_table);
            FREE(item);
            item = next;
        }
        return;
    }
    PTHREAD_COND_BROADCAST(&q_not_empty);
    PTHREAD_MUTEX_UNLOCK(&q_lock);
}

int Preload_queue_depth(void)
{
    PTHREAD_MUTEX_LOCK(&q_lock);
    int depth = q_depth;
    PTHREAD_MUTEX_UNLOCK(&q_lock);
    return depth;
}
