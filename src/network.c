#include "network.h"

#include "cache.h"
#include "config.h"
#include "log.h"

#include <openssl/crypto.h>

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/*
 * ----------------- External variables ----------------------
 */
CURLSH *CURL_SHARE;

/*
 * ----------------- Static variable -----------------------
 */
/** \brief curl multi interface handle */
static CURLM *curl_multi;
/** \brief  mutex for transfer functions */
static pthread_mutex_t transfer_lock;
/** \brief the lock array for cryptographic functions */
static pthread_mutex_t *crypto_lockarray;
/** \brief mutex for curl share interface itself */
static pthread_mutex_t curl_lock;

/*
 * -------------------- Functions --------------------------
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
/**
 * \brief OpenSSL 1.02 cryptography callback function
 * \details Required for OpenSSL 1.02, but not OpenSSL 1.1
 */
static void crypto_lock_callback(int mode, int type, char *file, int line)
{
    (void) file;
    (void) line;
    if (mode & CRYPTO_LOCK) {
        PTHREAD_MUTEX_LOCK(&(crypto_lockarray[type]));
    } else {
        PTHREAD_MUTEX_UNLOCK(&(crypto_lockarray[type]));
    }
}

/**
 * \brief OpenSSL 1.02 thread ID function
 * \details Required for OpenSSL 1.02, but not OpenSSL 1.1
 */
static unsigned long thread_id(void)
{
    unsigned long ret;

    ret = (unsigned long) pthread_self();
    return ret;
}

#pragma GCC diagnostic pop

static void crypto_lock_init(void)
{
    int i;

    crypto_lockarray =
        (pthread_mutex_t *) OPENSSL_malloc(CRYPTO_num_locks() *
                                           sizeof(pthread_mutex_t));
    for (i = 0; i < CRYPTO_num_locks(); i++) {
        if (pthread_mutex_init(&(crypto_lockarray[i]), NULL)) {
            lprintf(fatal, "crypto_lockarray[%d] initialisation \
failed!\n", i);
        };
    }

    CRYPTO_set_id_callback((unsigned long (*)()) thread_id);
    CRYPTO_set_locking_callback((void (*)()) crypto_lock_callback);
}

/**
 * \brief Curl share handle callback function
 * \details Adapted from:
 * https://curl.haxx.se/libcurl/c/threaded-shared-conn.html
 */
static void
curl_callback_lock(CURL * handle, curl_lock_data data,
                   curl_lock_access access, void *userptr)
{
    (void) access;              /* unused */
    (void) userptr;             /* unused */
    (void) handle;              /* unused */
    (void) data;                /* unused */
    PTHREAD_MUTEX_LOCK(&curl_lock);
}

static void
curl_callback_unlock(CURL * handle, curl_lock_data data, void *userptr)
{
    (void) userptr;             /* unused */
    (void) handle;              /* unused */
    (void) data;                /* unused */
    PTHREAD_MUTEX_UNLOCK(&curl_lock);
}

/**
 * \brief Process a curl message
 * \details Adapted from:
 * https://curl.haxx.se/libcurl/c/10-at-a-time.html
 */
static void
curl_process_msgs(CURLMsg * curl_msg, int n_running_curl, int n_mesgs)
{
    (void) n_running_curl;
    (void) n_mesgs;
    static volatile int slept = 0;
    if (curl_msg->msg == CURLMSG_DONE) {
        TransferStatusStruct *transfer;
        CURL *curl = curl_msg->easy_handle;
        curl_easy_getinfo(curl_msg->easy_handle, CURLINFO_PRIVATE,
                          &transfer);
        transfer->transferring = 0;
        char *url = NULL;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);

        /*
         * Wait for 5 seconds if we get HTTP 429
         */
        long http_resp = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
        if (HTTP_temp_failure(http_resp)) {
            if (!slept) {
                lprintf(warning,
                        "HTTP %ld, sleeping for %d sec\n",
                        http_resp, CONFIG.http_wait_sec);
                sleep(CONFIG.http_wait_sec);
                slept = 1;
            }
        } else {
            slept = 0;
        }

        if (!curl_msg->data.result) {
            /*
             * Transfer successful, set the file size
             */
            if (transfer->type == FILESTAT) {
                Link_set_file_stat(transfer->link, curl);
            }
        } else {
            lprintf(error, "%d - %s <%s>\n",
                    curl_msg->data.result,
                    curl_easy_strerror(curl_msg->data.result), url);
        }
        curl_multi_remove_handle(curl_multi, curl);
        /*
         * clean up the handle, if we are querying the file size
         */
        if (transfer->type == FILESTAT) {
            curl_easy_cleanup(curl);
            FREE(transfer);
        }
    } else {
        lprintf(warning, "curl_msg->msg: %d\n", curl_msg->msg);
    }
}

/**
 * \details  effectively based on
 * https://curl.haxx.se/libcurl/c/multi-double.html
 */
int curl_multi_perform_once(void)
{
    lprintf(network_lock_debug,
            "thread %x: locking transfer_lock;\n", pthread_self());
    PTHREAD_MUTEX_LOCK(&transfer_lock);

    /*
     * Get curl multi interface to perform pending tasks
     */
    int n_running_curl;
    CURLMcode mc = curl_multi_perform(curl_multi, &n_running_curl);
    if (mc > 0) {
        lprintf(error, "%s\n", curl_multi_strerror(mc));
    }

    fd_set fdread;
    fd_set fdwrite;
    fd_set fdexcep;
    int maxfd = -1;

    long curl_timeo = -1;

    FD_ZERO(&fdread);
    FD_ZERO(&fdwrite);
    FD_ZERO(&fdexcep);

    /*
     * set a default timeout for select()
     */
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    curl_multi_timeout(curl_multi, &curl_timeo);
    /*
     * We effectively cap timeout to 1 sec
     */
    if (curl_timeo >= 0) {
        timeout.tv_sec = curl_timeo / 1000;
        if (timeout.tv_sec > 1) {
            timeout.tv_sec = 1;
        } else {
            timeout.tv_usec = (curl_timeo % 1000) * 1000;
        }
    }

    /*
     * get file descriptors from the transfers
     */
    mc = curl_multi_fdset(curl_multi, &fdread, &fdwrite, &fdexcep, &maxfd);

    if (mc > 0) {
        lprintf(error, "%s.\n", curl_multi_strerror(mc));
    }

    if (maxfd == -1) {
        usleep(100 * 1000);
    } else {
        if (select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout) < 0) {
            lprintf(error, "select(): %s.\n", strerror(errno));
        }
    }

    /*
     * Process the message queue
     */
    int n_mesgs;
    CURLMsg *curl_msg;
    while ((curl_msg = curl_multi_info_read(curl_multi, &n_mesgs))) {
        curl_process_msgs(curl_msg, n_running_curl, n_mesgs);
    }

    lprintf(network_lock_debug,
            "thread %x: unlocking transfer_lock;\n", pthread_self());
    PTHREAD_MUTEX_UNLOCK(&transfer_lock);

    return n_running_curl;
}

void NetworkSystem_init(void)
{
    /*
     * ------- Global related ----------
     */
    if (curl_global_init(CURL_GLOBAL_ALL)) {
        lprintf(fatal, "curl_global_init() failed!\n");
    }

    /*
     * -------- Share related ----------
     */
    CURL_SHARE = curl_share_init();
    if (!(CURL_SHARE)) {
        lprintf(fatal, "curl_share_init() failed!\n");
    }

    curl_share_setopt(CURL_SHARE, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
    curl_share_setopt(CURL_SHARE, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(CURL_SHARE, CURLSHOPT_SHARE,
                      CURL_LOCK_DATA_SSL_SESSION);

    if (pthread_mutex_init(&curl_lock, NULL)) {
        lprintf(fatal, "curl_lock initialisation failed!\n");
    }
    curl_share_setopt(CURL_SHARE, CURLSHOPT_LOCKFUNC, curl_callback_lock);
    curl_share_setopt(CURL_SHARE, CURLSHOPT_UNLOCKFUNC,
                      curl_callback_unlock);

    /*
     * ------------- Multi related -----------
     */
    curl_multi = curl_multi_init();
    if (!curl_multi) {
        lprintf(fatal, "curl_multi_init() failed!\n");
    }
    curl_multi_setopt(curl_multi, CURLMOPT_MAX_TOTAL_CONNECTIONS,
                      CONFIG.max_conns);
    curl_multi_setopt(curl_multi, CURLMOPT_MAX_HOST_CONNECTIONS,
                      CONFIG.max_conns);

    /*
     * ------------ Initialise locks ---------
     */
    if (pthread_mutex_init(&transfer_lock, NULL)) {
        lprintf(fatal, "transfer_lock initialisation failed!\n");
    }

    /*
     * cryptographic lock functions were shamelessly copied from
     * https://curl.haxx.se/libcurl/c/threaded-ssl.html
     */
    crypto_lock_init();
}

void transfer_blocking(CURL * curl)
{
    /*
     * We don't need to malloc here, as the transfer is finished before
     * the variable gets popped from the stack
     */
    volatile TransferStatusStruct transfer;
    transfer.type = DATA;
    transfer.transferring = 1;
    curl_easy_setopt(curl, CURLOPT_PRIVATE, &transfer);

    lprintf(network_lock_debug,
            "thread %x: locking transfer_lock;\n", pthread_self());
    PTHREAD_MUTEX_LOCK(&transfer_lock);

    CURLMcode res = curl_multi_add_handle(curl_multi, curl);

    lprintf(network_lock_debug,
            "thread %x: unlocking transfer_lock;\n", pthread_self());
    PTHREAD_MUTEX_UNLOCK(&transfer_lock);

    if (res > 0) {
        lprintf(error, "%d, %s\n", res, curl_multi_strerror(res));
    }

    while (transfer.transferring) {
        curl_multi_perform_once();
    }
}

void transfer_nonblocking(CURL * curl)
{
    lprintf(network_lock_debug,
            "thread %x: locking transfer_lock;\n", pthread_self());
    PTHREAD_MUTEX_LOCK(&transfer_lock);

    CURLMcode res = curl_multi_add_handle(curl_multi, curl);

    lprintf(network_lock_debug,
            "thread %x: unlocking transfer_lock;\n", pthread_self());
    PTHREAD_MUTEX_UNLOCK(&transfer_lock);

    if (res > 0) {
        lprintf(error, "%s\n", curl_multi_strerror(res));
    }
}

size_t
write_memory_callback(void *contents, size_t size, size_t nmemb,
                      void *userp)
{
    size_t realsize = size * nmemb;
    TransferDataStruct *mem = (TransferDataStruct *) userp;

    mem->data = realloc(mem->data, mem->size + realsize + 1);
    if (!mem->data) {
        /*
         * out of memory!
         */
        lprintf(fatal, "realloc failure!\n");
    }

    memmove(&mem->data[mem->size], contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;
    // lprintf(debug, "realsize %d bytes\n", realsize);
    return realsize;
}

int HTTP_temp_failure(HTTPResponseCode http_resp)
{
    switch (http_resp) {
    case HTTP_TOO_MANY_REQUESTS:
    case HTTP_CLOUDFLARE_UNKNOWN_ERROR:
    case HTTP_CLOUDFLARE_TIMEOUT:
        return 1;
    default:
        return 0;
    }
}
