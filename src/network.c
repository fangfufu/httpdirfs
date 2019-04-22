#include "network.h"

#include <openssl/crypto.h>

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define NETWORK_MAX_CONNS 10

/* ----------------- External variables ---------------------- */
CURLSH *CURL_SHARE;
NetworkConfigStruct NETWORK_CONFIG;

/* ----------------- Static variable ----------------------- */
/** \brief curl multi interface handle */
static CURLM *curl_multi;
/** \brief  mutex for transfer functions */
static pthread_mutex_t transfer_lock;
/** \brief the lock array for cryptographic functions */
static pthread_mutex_t *crypto_lockarray;
/** \brief mutex for curl share interface itself */
static pthread_mutex_t curl_lock;
/** \brief network configuration */

/* -------------------- Functions -------------------------- */
static void crypto_lock_callback(int mode, int type, char *file, int line)
{
    (void)file;
    (void)line;
    if(mode & CRYPTO_LOCK) {
        pthread_mutex_lock(&(crypto_lockarray[type]));
    } else {
        pthread_mutex_unlock(&(crypto_lockarray[type]));
    }
}

static unsigned long thread_id(void)
{
    unsigned long ret;

    ret = (unsigned long)pthread_self();
    return ret;
}

static void crypto_lock_init(void)
{
    int i;

    crypto_lockarray = (pthread_mutex_t *)OPENSSL_malloc(CRYPTO_num_locks() *
                       sizeof(pthread_mutex_t));
    for(i = 0; i<CRYPTO_num_locks(); i++) {
        pthread_mutex_init(&(crypto_lockarray[i]), NULL);
    }

    CRYPTO_set_id_callback((unsigned long (*)())thread_id);
    CRYPTO_set_locking_callback((void (*)())crypto_lock_callback);
}

/**
 * Adapted from:
 * https://curl.haxx.se/libcurl/c/10-at-a-time.html
 */
static void curl_callback_lock(CURL *handle, curl_lock_data data,
                               curl_lock_access access, void *userptr)
{
    (void)access; /* unused */
    (void)userptr; /* unused */
    (void)handle; /* unused */
    (void)data; /* unused */
    pthread_mutex_lock(&curl_lock);
}

static void curl_callback_unlock(CURL *handle, curl_lock_data data,
                                 void *userptr)
{
    (void)userptr; /* unused */
    (void)handle;  /* unused */
    (void)data;    /* unused */
    pthread_mutex_unlock(&curl_lock);
}

static void curl_process_msgs(CURLMsg *curl_msg, int n_running_curl, int n_mesgs)
{
    (void) n_running_curl;
    (void) n_mesgs;
    if (curl_msg->msg == CURLMSG_DONE) {
        TransferStruct *transfer;
        CURL *curl = curl_msg->easy_handle;
        curl_easy_getinfo(curl_msg->easy_handle, CURLINFO_PRIVATE,
                          &transfer);
        transfer->transferring = 0;
        char *url = NULL;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);
        if (curl_msg->data.result) {
            fprintf(stderr, "curl_process_msgs(): %d - %s <%s>\n",
                    curl_msg->data.result,
                    curl_easy_strerror(curl_msg->data.result),
                    url);
            usleep(1000);
        } else {
            /* Transfer successful, query the file size */
            if (transfer->type == FILESTAT) {
//                 fprintf(stderr, "Link_set_stat(): %d, %d, %s\n",
//                         n_running_curl, n_mesgs, url);
                Link_set_stat(transfer->link, curl);
            }
        }
        curl_multi_remove_handle(curl_multi, curl);
        /* clean up the handle, if we are querying the file size */
        if (transfer->type == FILESTAT) {
            curl_easy_cleanup(curl);
            free(transfer);
        }
    } else {
        fprintf(stderr, "curl_process_msgs(): curl_msg->msg: %d\n",
                curl_msg->msg);
    }
}

int curl_multi_perform_once()
{
    pthread_mutex_lock(&transfer_lock);
    /* Get curl multi interface to perform pending tasks */
    int n_running_curl;
    curl_multi_perform(curl_multi, &n_running_curl);

    long timeout;
    if(curl_multi_timeout(curl_multi, &timeout)) {
        fprintf(stderr, "curl_multi_perform_once(): curl_multi_timeout\n");
        exit(EXIT_FAILURE);
    }

    if(timeout == -1) {
        /*
         * https://curl.haxx.se/libcurl/c/curl_multi_timeout.html
         * If it returns -1, there's no timeout at all set.
         */
        timeout = 0;
    }

    /* Check if any of the tasks encountered error */
    int max_fd;
    fd_set read_fd_set;
    fd_set write_fd_set;
    fd_set exc_fd_set;
    FD_ZERO(&read_fd_set);
    FD_ZERO(&write_fd_set);
    FD_ZERO(&exc_fd_set);
    CURLMcode res;
    res = curl_multi_fdset(curl_multi, &read_fd_set, &write_fd_set, &exc_fd_set,
                           &max_fd);
    if(res > 0) {
        fprintf(stderr, "curl_multi_perform_once(): curl_multi_fdset: %d, %s\n",
                res, curl_multi_strerror(res));
        exit(EXIT_FAILURE);
    }

    if(max_fd == -1) {
        /*
         * https://curl.haxx.se/libcurl/c/curl_multi_fdset.html
         * The above web page suggests sleeping for 100ms, unless
         * curl_multi_timeout() suggests something shorter.
         */
        if (timeout > 100) {
            timeout = 100;
        }
    }

    /* timeout is in miliseconds */
    struct timeval t;
    t.tv_sec = timeout/1000;            /* seconds      */
    t.tv_usec = (timeout%1000)*1000;    /* microseconds */

    if(select(max_fd + 1, &read_fd_set, &write_fd_set,
              &exc_fd_set, &t) < 0) {
        fprintf(stderr,
                "curl_multi_perform_once(): select(%i,,,,%li): %i: %s\n",
                max_fd + 1, timeout, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Process the message queue */
    int n_mesgs;
    CURLMsg *curl_msg;
    while((curl_msg = curl_multi_info_read(curl_multi, &n_mesgs))) {
        curl_process_msgs(curl_msg, n_running_curl, n_mesgs);
    }
    pthread_mutex_unlock(&transfer_lock);
    return n_running_curl;
}



void network_config_init()
{
    NETWORK_CONFIG.username = NULL;
    NETWORK_CONFIG.password = NULL;
    NETWORK_CONFIG.proxy = NULL;
    NETWORK_CONFIG.proxy_user = NULL;
    NETWORK_CONFIG.proxy_pass = NULL;
    NETWORK_CONFIG.max_conns = NETWORK_MAX_CONNS;
}

LinkTable *network_init(const char *url)
{
    /* ------- Global related ----------*/
    if (curl_global_init(CURL_GLOBAL_ALL)) {
        fprintf(stderr, "network_init(): curl_global_init() failed!\n");
        exit(EXIT_FAILURE);
    }

    /* -------- Share related ----------*/
    CURL_SHARE = curl_share_init();
    if (!(CURL_SHARE)) {
        fprintf(stderr, "network_init(): curl_share_init() failed!\n");
        exit(EXIT_FAILURE);
    }
    curl_share_setopt(CURL_SHARE, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
    curl_share_setopt(CURL_SHARE, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(CURL_SHARE, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);

    if (pthread_mutex_init(&curl_lock, NULL) != 0) {
        printf(
            "network_init(): curl_lock initialisation failed!\n");
        exit(EXIT_FAILURE);
    }
    curl_share_setopt(CURL_SHARE, CURLSHOPT_LOCKFUNC, curl_callback_lock);
    curl_share_setopt(CURL_SHARE, CURLSHOPT_UNLOCKFUNC, curl_callback_unlock);

    /* ------------- Multi related -----------*/
    curl_multi = curl_multi_init();
    if (!curl_multi) {
        fprintf(stderr, "network_init(): curl_multi_init() failed!\n");
        exit(EXIT_FAILURE);
    }
    curl_multi_setopt(curl_multi, CURLMOPT_MAX_TOTAL_CONNECTIONS,
                      NETWORK_CONFIG.max_conns);
    curl_multi_setopt(curl_multi, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);

    /* ------------ Initialise locks ---------*/
    if (pthread_mutex_init(&transfer_lock, NULL) != 0) {
        printf(
            "network_init(): transfer_lock initialisation failed!\n");
        exit(EXIT_FAILURE);
    }

    /*
     * cryptographic lock functions were shamelessly copied from
     * https://curl.haxx.se/libcurl/c/threaded-ssl.html
     */
    crypto_lock_init();

    /* --------- Print off SSL engine version stream --------- */
    curl_version_info_data *data = curl_version_info(CURLVERSION_NOW);
    fprintf(stderr, "libcurl SSL engine: %s\n", data->ssl_version);

    /* --------- Set the length of the root link ----------- */
    /* This is where the '/' should be */
    ROOT_LINK_OFFSET = strnlen(url, URL_LEN_MAX) - 1;
    if (url[ROOT_LINK_OFFSET] != '/') {
        /*
         * If '/' is not there, it is automatically added, so we need to skip 2
         * characters
         */
        ROOT_LINK_OFFSET += 2;
    } else {
        /* If '/' is there, we need to skip it */
        ROOT_LINK_OFFSET += 1;
    }

    /* ----------- Create the root link table --------------*/
    ROOT_LINK_TBL = LinkTable_new(url);
    return ROOT_LINK_TBL;
}

void transfer_blocking(CURL *curl)
{
    /*
     * We don't need to malloc here, as the transfer is finished before
     * the variable gets popped from the stack
     */
    volatile TransferStruct transfer;
    transfer.type = DATA;
    transfer.transferring = 1;
    curl_easy_setopt(curl, CURLOPT_PRIVATE, &transfer);

    pthread_mutex_lock(&transfer_lock);
    CURLMcode res = curl_multi_add_handle(curl_multi, curl);
    pthread_mutex_unlock(&transfer_lock);

    if(res > 0) {
        fprintf(stderr, "blocking_multi_transfer(): %d, %s\n",
                res, curl_multi_strerror(res));
        exit(EXIT_FAILURE);
    }

    while (transfer.transferring) {
        curl_multi_perform_once();
        usleep(1000);
    }
}

void transfer_nonblocking(CURL *curl)
{
    pthread_mutex_lock(&transfer_lock);
    CURLMcode res = curl_multi_add_handle(curl_multi, curl);
    pthread_mutex_unlock(&transfer_lock);

    if(res > 0) {
        fprintf(stderr, "blocking_multi_transfer(): %d, %s\n",
                res, curl_multi_strerror(res));
        exit(EXIT_FAILURE);
    }
}

size_t
write_memory_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    MemoryStruct *mem = (MemoryStruct *)userp;

    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if(!mem->memory) {
        /* out of memory! */
        fprintf(stderr, "write_memory_callback(): realloc failure!\n");
        exit(EXIT_FAILURE);
        return 0;
    }

    memmove(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}
