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
        ActiveDownload *ad = ts->cache_ptr->active_dls;
        while (ad) {
            if (ad->ts == ts) {
                PTHREAD_COND_BROADCAST(&ad->cond);
                break;
            }
            ad = ad->next;
        }
        PTHREAD_MUTEX_UNLOCK(&ts->cache_ptr->dl_lock);
    }

    return recv_size;
}
