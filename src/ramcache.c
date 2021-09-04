#include "ramcache.h"

#include "log.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

size_t write_memory_callback(void *recv_data, size_t size, size_t nmemb,
                             void *userp)
{
    size_t recv_size = size * nmemb;
    TransferStruct *ts = (TransferStruct *) userp;
    // lprintf(ramcache_debug, "bg_transfer: %d\n", ts->bg_transfer);
    // if (ts->bg_transfer) {
    //     lprintf(ramcache_debug, "ramcache: thread %x: locking;\n",
    //             pthread_self());
    // }
    // PTHREAD_MUTEX_LOCK(&ts->lock);

    ts->data = realloc(ts->data, ts->curr_size + recv_size + 1);
    if (!ts->data) {
        /*
         * out of memory!
         */
        lprintf(fatal, "realloc failure!\n");
    }

    memmove(&ts->data[ts->curr_size], recv_data, recv_size);
    ts->curr_size += recv_size;
    ts->data[ts->curr_size] = '\0';
    // if (ts->bg_transfer) {
    //     lprintf(ramcache_debug, "ramcache: thread %x: unlocking;\n",
    //             pthread_self());
    // }
    // PTHREAD_MUTEX_UNLOCK(&ts->lock);
    return recv_size;
}

TransferStruct *TransferStruct_bg_transfer_new()
{
    TransferStruct *ts = CALLOC(1, sizeof(TransferStruct));
    if (pthread_mutexattr_init(&ts->lock_attr)) {
        lprintf(fatal, "lock_attr initialisation failed!\n");
    }

    if (pthread_mutexattr_setpshared(&ts->lock_attr,
                                     PTHREAD_PROCESS_SHARED)) {
        lprintf(fatal, "could not set lock_attr!\n");
    }

    if (pthread_mutex_init(&ts->lock, &ts->lock_attr)) {
        lprintf(fatal, "lock initialisation failed!\n");
    }

    ts->bg_transfer = 1;

    return ts;
}

void TransferStruct_bg_transfer_free(TransferStruct *ts)
{
    if (ts->bg_transfer) {
        if (pthread_mutex_destroy(&ts->lock)) {
            lprintf(fatal, "could not destroy lock!\n");
        }

        if (pthread_mutexattr_destroy(&ts->lock_attr)) {
            lprintf(fatal, "could not destroy lock_attr!\n");
        }
    }

    /* free(NULL) does nothing */
    free(ts->data);
    FREE(ts);
}

