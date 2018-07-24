#include "network.h"

#include <curl/curl.h>
#include <gumbo.h>
#include <openssl/crypto.h>

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>


#define HTTP_OK 200
#define HTTP_PARTIAL_CONTENT 206
#define HTTP_RANGE_NOT_SATISFIABLE 416

/* ------------------------ External variables ----------------------------*/
LinkTable *ROOT_LINK_TBL;

/* ------------------------ Local structs ---------------------------------*/
typedef struct {
    char *memory;
    size_t size;
} MemoryStruct;

typedef enum {
    FILESTAT = 's',
    DATA = 'd'
} TransferType;

typedef struct {
    TransferType type;
    int transferring;
    Link *link;
} TransferStruct;

/* ------------------------ Static variable ------------------------------ */
/** \brief curl shared interface - not sure if is being used properly. */
static CURLSH *curl_share;
/** \brief curl multi interface handle */
static CURLM *curl_multi;
/** \brief pthread mutex for transfer functions */
static pthread_mutex_t transfer_lock;

/** \brief the lock array for cryptographic functions */
static pthread_mutex_t *crypto_lockarray;
/**
 * \brief pthread mutex for curl itself
 * \note I am sure if it is being used properly
 */
static pthread_mutex_t curl_lock;

/* -------------------------- Functions ---------------------------------- */
#pragma GCC diagnostic ignored "-Wunused-function"
static void lock_callback(int mode, int type, char *file, int line)
{
#pragma GCC diagnostic pop
    (void)file;
    (void)line;
    if(mode & CRYPTO_LOCK) {
        pthread_mutex_lock(&(crypto_lockarray[type]));
    }
    else {
        pthread_mutex_unlock(&(crypto_lockarray[type]));
    }
}
#pragma GCC diagnostic ignored "-Wunused-function"
static unsigned long thread_id(void)
{
#pragma GCC diagnostic pop
    unsigned long ret;

    ret = (unsigned long)pthread_self();
    return ret;
}

static void init_locks(void)
{
    int i;

    crypto_lockarray = (pthread_mutex_t *)OPENSSL_malloc(CRYPTO_num_locks() *
    sizeof(pthread_mutex_t));
    for(i = 0; i<CRYPTO_num_locks(); i++) {
        pthread_mutex_init(&(crypto_lockarray[i]), NULL);
    }

    CRYPTO_set_id_callback((unsigned long (*)())thread_id);
    CRYPTO_set_locking_callback((void (*)())lock_callback);
}

static void pthread_lock_cb(CURL *handle, curl_lock_data data,
                            curl_lock_access access, void *userptr)
{
    (void)access; /* unused */
    (void)userptr; /* unused */
    (void)handle; /* unused */
    (void)data; /* unused */
    pthread_mutex_lock(&curl_lock);
}

static void pthread_unlock_cb(CURL *handle, curl_lock_data data,
                              void *userptr)
{
    (void)userptr; /* unused */
    (void)handle;  /* unused */
    (void)data;    /* unused */
    pthread_mutex_unlock(&curl_lock);
}

void network_init(const char *url)
{
    /*
     * Intialise the cryptographic locks, these are shamelessly copied from
     * https://curl.haxx.se/libcurl/c/threaded-ssl.html
     */
    init_locks();

    /* Global related */
    if (curl_global_init(CURL_GLOBAL_ALL)) {
        fprintf(stderr, "network_init(): curl_global_init() failed!\n");
        exit(EXIT_FAILURE);
    }

    /* Share related */
    curl_share = curl_share_init();
    if (!(curl_share)) {
        fprintf(stderr, "network_init(): curl_share_init() failed!\n");
        exit(EXIT_FAILURE);
    }
    curl_share_setopt(curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
    curl_share_setopt(curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(curl_share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);

    if (pthread_mutex_init(&curl_lock, NULL) != 0)
    {
        printf(
            "network_init(): curl_lock initialisation failed!\n");
        exit(EXIT_FAILURE);
    }
    curl_share_setopt(curl_share, CURLSHOPT_LOCKFUNC, pthread_lock_cb);
    curl_share_setopt(curl_share, CURLSHOPT_UNLOCKFUNC, pthread_unlock_cb);

    /* Multi related */
    curl_multi = curl_multi_init();
    if (!curl_multi) {
        fprintf(stderr, "network_init(): curl_multi_init() failed!\n");
        exit(EXIT_FAILURE);
    }
    curl_multi_setopt(curl_multi, CURLMOPT_MAXCONNECTS,
                      CURL_MULTI_MAX_CONNECTION);

    /* Initialise transfer lock */
    if (pthread_mutex_init(&transfer_lock, NULL) != 0)
    {
        printf(
            "network_init(): transfer_lock initialisation failed!\n");
        exit(EXIT_FAILURE);
    }

    curl_version_info_data *data = curl_version_info(CURLVERSION_NOW);
    printf("libcurl SSL engine: %s\n", data->ssl_version);

    /* create the root link table */
    ROOT_LINK_TBL = LinkTable_new(url);
}

static void link_set_stat(Link* this_link, CURL *curl)
{
    long http_resp;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
    if (http_resp == HTTP_OK) {
        double cl = 0;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &cl);
        curl_easy_getinfo(curl, CURLINFO_FILETIME, &(this_link->time));

        if (cl == -1) {
            /* Turns out not to be a file after all */
            this_link->content_length = 0;
            this_link->type = LINK_DIR;
        } else {
            this_link->content_length = cl;
            this_link->type = LINK_FILE;
        }
    } else {
        this_link->type = LINK_INVALID;
    }
}

/**
 * Shamelessly copy and pasted from:
 * https://curl.haxx.se/libcurl/c/10-at-a-time.html
 */
static int curl_multi_perform_once()
{
    pthread_mutex_lock(&transfer_lock);
    /* Get curl multi interface to perform pending tasks */
    int n_running_curl;
    curl_multi_perform(curl_multi, &n_running_curl);

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

    long timeout;
    if(curl_multi_timeout(curl_multi, &timeout)) {
        fprintf(stderr, "curl_multi_perform_once(): curl_multi_timeout\n");
        exit(EXIT_FAILURE);
    }

    if(timeout == -1) {
        timeout = 100;
    }

    if(max_fd == -1) {
        sleep(1);
    } else {
        struct timeval t;
        t.tv_sec = timeout/1000;
        t.tv_usec = (timeout%1000)*1000;

        if(select(max_fd + 1, &read_fd_set, &write_fd_set,
            &exc_fd_set, &t) < 0) {
            fprintf(stderr,
                    "curl_multi_perform_once(): select(%i,,,,%li): %i: %s\n",
                    max_fd + 1, timeout, errno, strerror(errno));
            exit(EXIT_FAILURE);
            }
    }

    /* Process messages */
    int n_mesgs;
    CURLMsg *curl_msg;
    while((curl_msg = curl_multi_info_read(curl_multi, &n_mesgs))) {
        if (curl_msg->msg == CURLMSG_DONE) {
            TransferStruct *transfer;
            CURL *curl = curl_msg->easy_handle;
            curl_easy_getinfo(curl_msg->easy_handle, CURLINFO_PRIVATE,
                              &transfer);
            transfer->transferring = 0;
            char *url = NULL;
            if (curl_msg->data.result) {
                curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, url);
                fprintf(stderr,
                        "curl_multi_perform_once(): %d - %s <%s>\n",
                        curl_msg->data.result,
                        curl_easy_strerror(curl_msg->data.result),
                        url);
                sleep(1);
            } else {
                /* Transfer successful, query the file size */
                if (transfer->type == FILESTAT) {
                    link_set_stat(transfer->link, curl);
                }
            }
            curl_multi_remove_handle(curl_multi, curl);
            /* clean up the handle, if we are querying the file size */
            if (transfer->type == FILESTAT) {
                curl_easy_cleanup(curl);
                free(transfer);
            }
        } else {
            fprintf(stderr, "curl_multi_perform_once(): curl_msg->msg: %d\n",
                    curl_msg->msg);
        }
    }
    pthread_mutex_unlock(&transfer_lock);
    return n_running_curl;
}

static void nonblocking_transfer(CURL *curl)
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

/* This uses the curl multi interface */
static void blocking_transfer(CURL *curl)
{
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
    }
}

static char *url_append(const char *url, const char *sublink)
{
    int needs_separator = 0;
    if (url[strlen(url)-1] != '/') {
        needs_separator = 1;
    }

    char *str;
    size_t ul = strlen(url);
    size_t sl = strlen(sublink);
    str = calloc(ul + sl + needs_separator + 1, sizeof(char));
    if (!str) {
        fprintf(stderr, "url_append(): calloc failure!\n");
        exit(EXIT_FAILURE);
    }
    strncpy(str, url, ul);
    if (needs_separator) {
        str[ul] = '/';
    }
    strncat(str, sublink, sl);
    return str;
}

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    MemoryStruct *mem = (MemoryStruct *)userp;

    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if(!mem->memory) {
        /* out of memory! */
        fprintf(stderr, "WriteMemoryCallback(): realloc failure!\n");
        exit(EXIT_FAILURE);
        return 0;
    }

    memmove(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

static Link *Link_new(const char *p_url, LinkType type)
{
    Link *link = calloc(1, sizeof(Link));
    if (!link) {
        fprintf(stderr, "Link_new(): calloc failure!\n");
        exit(EXIT_FAILURE);
    }
    strncpy(link->p_url, p_url, LINK_LEN_MAX);
    link->type = type;

    /* remove the '/' from p_url if it exists */
    char *c = &(link->p_url[strnlen(link->p_url, LINK_LEN_MAX) - 1]);
    if ( *c == '/') {
        *c = '\0';
    }

    return link;
}

static CURL *Link_to_curl(Link *link)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Link_to_curl(): curl_easy_init() failed!\n");
    }

    /* set up some basic curl stuff */
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "mount-http-dir/libcurl");
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    /* for following directories without the '/' */
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 2);
    curl_easy_setopt(curl, CURLOPT_URL, link->f_url);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1);
    curl_easy_setopt(curl, CURLOPT_SHARE, curl_share);
    /*
     * The write back function pointer has to be set at curl handle creation,
     * for thread safety
     */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

    return curl;
}

long path_download(const char *path, char *output_buf, size_t size,
                     off_t offset)
{
    Link *link;
    link = path_to_Link(path);
    if (!link) {
        return -ENOENT;
    }

    size_t start = offset;
    size_t end = start + size;
    char range_str[64];
    snprintf(range_str, sizeof(range_str), "%lu-%lu", start, end);

    MemoryStruct buf;
    buf.size = 0;
    buf.memory = NULL;

#ifdef HTTPDIRFS_INFO
    fprintf(stderr, "path_download(%s, %p, %s);\n",
            path, output_buf, range_str);
#endif
    CURL *curl = Link_to_curl(link);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);
    curl_easy_setopt(curl, CURLOPT_RANGE, range_str);

    blocking_transfer(curl);

    long http_resp;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
    if ( !(
            (http_resp != HTTP_OK) ||
            (http_resp != HTTP_PARTIAL_CONTENT) ||
            (http_resp != HTTP_RANGE_NOT_SATISFIABLE)
          )) {
        fprintf(stderr, "path_download(): Could not download %s, HTTP %ld\n",
        link->f_url, http_resp);
        return -ENOENT;
    }

    double dl;
    curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &dl);

    size_t recv = dl;
    if (recv > size) {
        recv = size;
    }

    memmove(output_buf, buf.memory, recv);
    curl_easy_cleanup(curl);
    free(buf.memory);
    return recv;
}

static void LinkTable_free(LinkTable *linktbl)
{
    for (int i = 0; i < linktbl->num; i++) {
        free(linktbl->links[i]);
    }
    free(linktbl->links);
    free(linktbl);
}

static LinkType p_url_type(const char *p_url)
{
    /* The link name has to start with alphanumerical character */
    if (!isalnum(p_url[0])) {
        return LINK_INVALID;
    }

    /* check for http:// and https:// */
    if ( !strncmp(p_url, "http://", 7) || !strncmp(p_url, "https://", 8) ) {
        return LINK_INVALID;
    }

    if ( p_url[strlen(p_url) - 1] == '/' ) {
        return LINK_DIR;
    }

    return LINK_FILE;
}

static void LinkTable_add(LinkTable *linktbl, Link *link)
{
    linktbl->num++;
    linktbl->links = realloc(linktbl->links, linktbl->num * sizeof(Link *));
    if (!linktbl->links) {
        fprintf(stderr, "LinkTable_add(): realloc failure!\n");
        exit(EXIT_FAILURE);
    }
    linktbl->links[linktbl->num - 1] = link;
}

/**
 * Shamelessly copied and pasted from:
 * https://github.com/google/gumbo-parser/blob/master/examples/find_links.cc
 */
static void HTML_to_LinkTable(GumboNode *node, LinkTable *linktbl)
{
    if (node->type != GUMBO_NODE_ELEMENT) {
        return;
    }
    GumboAttribute* href;

    if (node->v.element.tag == GUMBO_TAG_A &&
        (href = gumbo_get_attribute(&node->v.element.attributes, "href"))) {
        /* if it is valid, copy the link onto the heap */
        LinkType type = p_url_type(href->value);
        if (type) {
            LinkTable_add(linktbl, Link_new(href->value, type));
        }
    }
    /* Note the recursive call, lol. */
    GumboVector *children = &node->v.element.children;
    for (size_t i = 0; i < children->length; ++i) {
        HTML_to_LinkTable((GumboNode*)children->data[i], linktbl);
    }
    return;
}

void Link_get_stat(Link *this_link)
{
    #ifdef HTTPDIRFS_INFO
    fprintf(stderr, "Link_get_size(%s);\n", this_link->f_url);
    #endif

    if (this_link->type == LINK_FILE) {
        CURL *curl = Link_to_curl(this_link);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
        curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);

        TransferStruct *transfer = malloc(sizeof(TransferStruct));
        if (!transfer) {
            fprintf(stderr, "Link_get_size(): malloc failed!\n");
        }
        transfer->link = this_link;
        transfer->type = FILESTAT;
        curl_easy_setopt(curl, CURLOPT_PRIVATE, transfer);

        nonblocking_transfer(curl);
    }
}

void LinkTable_fill(LinkTable *linktbl)
{
    Link *head_link = linktbl->links[0];
    for (int i = 0; i < linktbl->num; i++) {
        Link *this_link = linktbl->links[i];
        if (this_link->type) {
            char *url;
            url = url_append(head_link->f_url, this_link->p_url);
            strncpy(this_link->f_url, url, URL_LEN_MAX);
            free(url);

            char *unescaped_p_url;
            unescaped_p_url = curl_easy_unescape(NULL, this_link->p_url, 0,
                                                 NULL);
            strncpy(this_link->p_url, unescaped_p_url, LINK_LEN_MAX);
            curl_free(unescaped_p_url);

            if (this_link->type == LINK_FILE && !(this_link->content_length)) {
                Link_get_stat(this_link);
            }
        }
    }
    /* Block until the LinkTable is filled up */
    while(curl_multi_perform_once());
}

LinkTable *LinkTable_new(const char *url)
{
#ifdef HTTPDIRFS_INFO
    fprintf(stderr, "LinkTable_new(%s);\n", url);
#endif
    LinkTable *linktbl = calloc(1, sizeof(LinkTable));
    if (!linktbl) {
        fprintf(stderr, "LinkTable_new(): calloc failure!\n");
        exit(EXIT_FAILURE);
    }

    /* populate the base URL */
    LinkTable_add(linktbl, Link_new("/", LINK_HEAD));
    Link *head_link = linktbl->links[0];
    head_link->type = LINK_HEAD;
    strncpy(head_link->f_url, url, URL_LEN_MAX);

    /* start downloading the base URL */
    CURL *curl = Link_to_curl(head_link);
    MemoryStruct buf;
    buf.size = 0;
    buf.memory = NULL;
//     curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);

    blocking_transfer(curl);

    /* if downloading base URL failed */
    long http_resp;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
    if (http_resp != HTTP_OK) {
        fprintf(stderr, "link.c: LinkTable_new() cannot retrive the base URL, \
URL: %s, HTTP %ld\n", url, http_resp);

        LinkTable_free(linktbl);
        linktbl = NULL;
        return linktbl;
    };
    curl_easy_cleanup(curl);

    /* Otherwise parsed the received data */
    GumboOutput* output = gumbo_parse(buf.memory);
    HTML_to_LinkTable(output->root, linktbl);
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    free(buf.memory);

    /* Fill in the link table */
    LinkTable_fill(linktbl);
    return linktbl;
}

#ifdef HTTPDIRFS_INFO
void LinkTable_print(LinkTable *linktbl)
{
    fprintf(stderr, "--------------------------------------------\n");
    fprintf(stderr, " LinkTable %p for %s\n", linktbl,
            linktbl->links[0]->f_url);
    fprintf(stderr, "--------------------------------------------\n");
    for (int i = 0; i < linktbl->num; i++) {
        Link *this_link = linktbl->links[i];
        fprintf(stderr, "%d %c %lu %s %p %s\n",
               i,
               this_link->type,
               this_link->content_length,
               this_link->p_url,
               this_link->next_table,
               this_link->f_url
              );

    }
    fprintf(stderr, "--------------------------------------------\n");
}
#endif

static Link *path_to_Link_recursive(char *path, LinkTable *linktbl)
{
    /* skip the leading '/' if it exists */
    if (*path == '/') {
        path++;
    }

    /* remove the last '/' if it exists */
    char *slash = &(path[strnlen(path, URL_LEN_MAX) - 1]);
    if (*slash == '/') {
        *slash = '\0';
    }

    slash = strchr(path, '/');
    if ( slash == NULL ) {
        /* We cannot find another '/', we have reached the last level */
        for (int i = 1; i < linktbl->num; i++) {
            if (!strncmp(path, linktbl->links[i]->p_url, LINK_LEN_MAX)) {
                /* We found our link */
                return linktbl->links[i];
            }
        }
    } else {
        /*
         * We can still find '/', time to consume the path and traverse
         * the tree structure
         */

        /*
         * add termination mark to  the current string,
         * effective create two substrings
         */
        *slash = '\0';
        /* move the pointer past the '/' */
        char *next_path = slash + 1;
        for (int i = 1; i < linktbl->num; i++) {
            if (!strncmp(path, linktbl->links[i]->p_url, LINK_LEN_MAX)) {
                /* The next sub-directory exists */
                if (!(linktbl->links[i]->next_table)) {
                    linktbl->links[i]->next_table = LinkTable_new(
                        linktbl->links[i]->f_url);
#ifdef HTTPDIRFS_INFO
                    fprintf(stderr, "Created new link table for %s\n",
                            linktbl->links[i]->f_url);
                    LinkTable_print(linktbl->links[i]->next_table);
#endif
                }

                return path_to_Link_recursive(next_path,
                                              linktbl->links[i]->next_table);
            }
        }
    }
    return NULL;
}

Link *path_to_Link(const char *path)
{
    char *new_path = strndup(path, URL_LEN_MAX);
    if (!new_path) {
        fprintf(stderr, "path_to_Link(): cannot allocate memory\n");
        exit(EXIT_FAILURE);
    }
    return path_to_Link_recursive(new_path, ROOT_LINK_TBL);
}


