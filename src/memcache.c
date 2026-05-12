#include "memcache.h"

#include "log.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

size_t write_memory_callback(void *recv_data, size_t size, size_t nmemb,
                             void *userp)
{
    size_t recv_size = size * nmemb;
    TransferStruct *ts = (TransferStruct *)userp;

    char *tmp = REALLOC(ts->data, ts->curr_size + recv_size + 1);
    if (!tmp) {
        /*
         * out of memory!
         */
        lprintf(fatal, "REALLOC failure!\n");
    }
    ts->data = tmp;

    memmove(&ts->data[ts->curr_size], recv_data, recv_size);
    ts->curr_size += recv_size;
    ts->data[ts->curr_size] = '\0';

    return recv_size;
}
