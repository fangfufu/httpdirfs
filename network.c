#include <ctype.h>
#include <string.h>
#include <errno.h>

#include "network.h"

#define NETWORK_MAXIMUM_CONNECTION 10

CURLM *curl_multi;

static void do_transfer(CURL *curl)
{
    /* Add the transfer handle */
    curl_multi_add_handle(curl_multi, curl);

    CURLMcode res;
    int num_transfers, max_fd;
    long timeout;
    fd_set read_fd_set, write_fd_set, exc_fd_set;
    do {
        res = curl_multi_perform(curl_multi, &num_transfers);
        if (res) {
            fprintf(stderr,
                    "do_transfer(): curl_multi_perform(): %s\n",
                    curl_multi_strerror(res));
        }
        if (!num_transfers) {
            break;
        }

        res = curl_multi_fdset(curl_multi, &read_fd_set, &write_fd_set,
                               &exc_fd_set, &max_fd);
        if (res) {
            fprintf(stderr,
                    "do_transfer(): curl_multi_fdset(): %s\n",
                    curl_multi_strerror(res));
        }

        res = curl_multi_timeout(curl_multi, &timeout);
        if (res) {
            fprintf(stderr,
                    "do_transfer(): curl_multi_timeout(): %s\n",
                    curl_multi_strerror(res));
        }

        if (max_fd < 0 || timeout < 0) {
            /*
             * To find out why, read:
             * https://curl.haxx.se/libcurl/c/curl_multi_fdset.html
             */
            timeout = 100;
        }


        struct timeval t;
        /* convert timeout (in millisec) to sec */
        t.tv_sec = timeout/1000;
        /* convert the remainder to microsec */
        t.tv_usec = (timeout%1000)*1000;

        /*
         * The select() system call blocks until one or more of a set of
         * file descriptors becomes ready.
         * (The Linux Programming Interface, Michael Kerrisk)
         *
         * See also:
         * https://curl.haxx.se/libcurl/c/curl_multi_timeout.html
         */
        if (select(max_fd + 1, &read_fd_set,
            &write_fd_set, &exc_fd_set, &t) <  0) {
            fprintf(stderr, "do_transfer(): select(%i, , , , %li): %i: %s\n",
                    max_fd + 1, timeout, errno, strerror(errno));
        }
    } while(num_transfers);

    /* Remove the transfer handle */
    curl_multi_remove_handle(curl_multi, curl);
}
static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    Link *mem = (Link *)userp;

    mem->body = realloc(mem->body, mem->body_sz + realsize + 1);
    if(mem->body == NULL) {
        /* out of memory! */
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }

    memcpy(&(mem->body[mem->body_sz]), contents, realsize);
    mem->body_sz += realsize;
    mem->body[mem->body_sz] = 0;

    return realsize;
}

void Network_init()
{
    curl_global_init(CURL_GLOBAL_ALL);
    curl_multi = curl_multi_init();
    curl_multi_setopt(curl_multi, CURLMOPT_MAXCONNECTS,
                      (long)NETWORK_MAXIMUM_CONNECTION);
}

Link *Link_new(const char *p_url)
{
    Link *link = calloc(1, sizeof(Link));

    strncpy(link->p_url, p_url, LINK_LEN_MAX);

    /* remove the '/' */
    char *c = &(link->p_url[strnlen(link->p_url, LINK_LEN_MAX) - 1]);
    if ( *c == '/') {
        *c = '\0';
    }

    link->type = LINK_UNKNOWN;

    link->curl = curl_easy_init();

    /* set up some basic curl stuff */
    curl_easy_setopt(link->curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(link->curl, CURLOPT_WRITEDATA, (void *)link);
    curl_easy_setopt(link->curl, CURLOPT_USERAGENT, "mount-http-dir/libcurl");
    curl_easy_setopt(link->curl, CURLOPT_VERBOSE, 0);
    curl_easy_setopt(link->curl, CURLOPT_FOLLOWLOCATION, 1);
    /*
     * only 1 redirection is really needed
     * - for following directories without the '/'
     */
    curl_easy_setopt(link->curl, CURLOPT_MAXREDIRS, 3);

    return link;
}

void Link_free(Link *link)
{
    curl_easy_cleanup(link->curl);
    free(link->body);
    free(link);
    link = NULL;
}

int Link_download(Link *link, size_t start, size_t end)
{
    CURL *curl = link->curl;
    char range_str[64];
    snprintf(range_str, sizeof(range_str), "%lu-%lu", start, end);

    curl_easy_setopt(curl, CURLOPT_NOBODY, 0);
    curl_easy_setopt(curl, CURLOPT_RANGE, range_str);
    do_transfer(curl);

    long http_resp;
    curl_easy_getinfo(link->curl, CURLINFO_RESPONSE_CODE, &http_resp);
    return http_resp;
}

LinkTable *LinkTable_new(const char *url)
{
    LinkTable *linktbl = calloc(1, sizeof(LinkTable));

    /* populate the base URL */
    LinkTable_add(linktbl, Link_new("/"));
    Link *head_link = linktbl->links[0];
    head_link->type = LINK_HEAD;
    curl_easy_setopt(head_link->curl, CURLOPT_URL, url);

    /* start downloading the base URL */
    do_transfer(head_link->curl);

    /* if downloading base URL failed */
    long http_resp;
    curl_easy_getinfo(head_link->curl, CURLINFO_RESPONSE_CODE, &http_resp);
    if (http_resp != HTTP_OK) {
        fprintf(stderr, "link.c: LinkTable_new() cannot retrive the base URL");
        LinkTable_free(linktbl);
        linktbl = NULL;
        return linktbl;
    };

    /* Otherwise parsed the received data */
    GumboOutput* output = gumbo_parse(head_link->body);
    HTML_to_LinkTable(output->root, linktbl);
    gumbo_destroy_output(&kGumboDefaultOptions, output);

    /* Fill in the link table */
    LinkTable_fill(linktbl);
    return linktbl;
}

void LinkTable_free(LinkTable *linktbl)
{
    for (int i = 0; i < linktbl->num; i++) {
        Link_free(linktbl->links[i]);
    }
    free(linktbl->links);
    free(linktbl);
    linktbl = NULL;
}

void LinkTable_add(LinkTable *linktbl, Link *link)
{
    linktbl->num++;
    linktbl->links = realloc(
        linktbl->links,
        linktbl->num * sizeof(Link *));
    linktbl->links[linktbl->num - 1] = link;
}

void LinkTable_fill(LinkTable *linktbl)
{
    Link *head_link = linktbl->links[0];
    for (int i = 0; i < linktbl->num; i++) {
        Link *this_link = linktbl->links[i];
        if (this_link->type == LINK_UNKNOWN) {
            CURL *curl = this_link->curl;
            char *url;
            curl_easy_getinfo(head_link->curl, CURLINFO_EFFECTIVE_URL, &url);
            url = url_append(url, this_link->p_url);
            curl_easy_setopt(curl, CURLOPT_URL, url);
            free(url);
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1);

            do_transfer(curl);

            long http_resp;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
            if (http_resp == HTTP_OK) {
                double cl;
                curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &cl);
                if (cl == -1) {
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
    }
}


void LinkTable_print(LinkTable *linktbl)
{
    for (int i = 0; i < linktbl->num; i++) {
        Link *this_link = linktbl->links[i];
        printf("%d %c %lu %s\n",
               i,
               this_link->type,
               this_link->content_length,
               this_link->p_url);
    }
}

static int is_valid_link(const char *n)
{
    /* The link name has to start with alphanumerical character */
    if (!isalnum(n[0])) {
        return 0;
    }

    /* check for http:// and https:// */
    int c = strnlen(n, LINK_LEN_MAX);
    if (c > 5) {
        if (n[0] == 'h' && n[1] == 't' && n[2] == 't' && n[3] == 'p') {
            if ((n[4] == ':' && n[5] == '/' && n[6] == '/') ||
                (n[4] == 's' && n[5] == ':' && n[6] == '/' && n[7] == '/')) {
                return 0;
            }
        }
    }
    return 1;
}

/*
 * Shamelessly copied and pasted from:
 * https://github.com/google/gumbo-parser/blob/master/examples/find_links.cc
 */
void HTML_to_LinkTable(GumboNode *node, LinkTable *linktbl)
{
    if (node->type != GUMBO_NODE_ELEMENT) {
        return;
    }
    GumboAttribute* href;

    if (node->v.element.tag == GUMBO_TAG_A &&
        (href = gumbo_get_attribute(&node->v.element.attributes, "href"))) {
        /* if it is valid, copy the link onto the heap */
        if (is_valid_link(href->value)) {
            LinkTable_add(linktbl, Link_new(href->value));
        }
    }

    /* Note the recursive call, lol. */
    GumboVector *children = &node->v.element.children;
    for (size_t i = 0; i < children->length; ++i) {
        HTML_to_LinkTable((GumboNode*)children->data[i], linktbl);
    }
    return;
}

/* the upper level */
char *url_upper(const char *url)
{
    const char *pt = strrchr(url, '/');
    /* +1 for the '/' */
    size_t  len = pt - url + 1;
    char *str = strndup(url, len);
    str[len] = '\0';
    return str;
}

/* append url */
char *url_append(const char *url, const char *sublink)
{
    int needs_separator = 0;
    if (url[strlen(url)-1] != '/') {
        needs_separator = 1;
    }

    char *str;
    size_t ul = strlen(url);
    size_t sl = strlen(sublink);
    str = calloc(ul + sl + needs_separator, sizeof(char));
    strncpy(str, url, ul);
    if (needs_separator) {
        str[ul] = '/';
    }
    strncat(str, sublink, sl);
    return str;
}
