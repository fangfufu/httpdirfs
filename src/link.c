#include "link.h"

#include "cache.h"
#include "network.h"

#include <gumbo.h>

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------------- External variables -----------------------*/
LinkTable *ROOT_LINK_TBL = NULL;
int ROOT_LINK_OFFSET = 0;

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

static Link *Link_new(const char *linkname, LinkType type)
{
    Link *link = calloc(1, sizeof(Link));
    if (!link) {
        fprintf(stderr, "Link_new(): calloc failure!\n");
        exit(EXIT_FAILURE);
    }
    strncpy(link->linkname, linkname, MAX_FILENAME_LEN);
    link->type = type;

    /* remove the '/' from linkname if it exists */
    char *c = &(link->linkname[strnlen(link->linkname, MAX_FILENAME_LEN) - 1]);
    if ( *c == '/') {
        *c = '\0';
    }

    return link;
}

static LinkType linkname_type(const char *linkname)
{
    /* The link name has to start with alphanumerical character */
    if (!isalnum(linkname[0])) {
        return LINK_INVALID;
    }

    /* check for http:// and https:// */
    if ( !strncmp(linkname, "http://", 7) || !strncmp(linkname, "https://", 8) ) {
        return LINK_INVALID;
    }

    if ( linkname[strnlen(linkname, MAX_FILENAME_LEN) - 1] == '/' ) {
        return LINK_DIR;
    }

    return LINK_FILE;
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
        LinkType type = linkname_type(href->value);
        if (type) {
            LinkTable_add(linktbl, Link_new(href->value, type));
        }
    }
    /* Note the recursive call, lol. */
    GumboVector *children = &node->v.element.children;
    for (size_t i = 0; i < children->length; ++i) {
        HTML_to_LinkTable((GumboNode *)children->data[i], linktbl);
    }
    return;
}

static CURL *Link_to_curl(Link *link)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Link_to_curl(): curl_easy_init() failed!\n");
    }
    /* set up some basic curl stuff */
    curl_easy_setopt(curl, CURLOPT_USERAGENT, NETWORK_CONFIG.user_agent);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    /* for following directories without the '/' */
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 2);
    curl_easy_setopt(curl, CURLOPT_URL, link->f_url);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15);
    curl_easy_setopt(curl, CURLOPT_SHARE, CURL_SHARE);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
//     curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);


    if (NETWORK_CONFIG.username) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, NETWORK_CONFIG.username);
    }

    if (NETWORK_CONFIG.password) {
        curl_easy_setopt(curl, CURLOPT_PASSWORD, NETWORK_CONFIG.password);
    }

    if (NETWORK_CONFIG.proxy) {
        curl_easy_setopt(curl, CURLOPT_PROXY, NETWORK_CONFIG.proxy);
    }

    if (NETWORK_CONFIG.proxy_user) {
        curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME,
                         NETWORK_CONFIG.proxy_user);
    }

    if (NETWORK_CONFIG.proxy_pass) {
        curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD,
                         NETWORK_CONFIG.proxy_pass);
    }

    return curl;
}

void Link_get_stat(Link *this_link)
{
    if (this_link->type == LINK_FILE) {
        CURL *curl = Link_to_curl(this_link);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
        curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);

        /*
         * We need to put the variable on the heap, because otherwise the
         * variable gets popped from the stack as the function returns.
         *
         * It gets freed in curl_multi_perform_once();
         */
        TransferStruct *transfer = malloc(sizeof(TransferStruct));
        if (!transfer) {
            fprintf(stderr, "Link_get_size(): malloc failed!\n");
            exit(EXIT_FAILURE);
        }
        transfer->link = this_link;
        transfer->type = FILESTAT;
        curl_easy_setopt(curl, CURLOPT_PRIVATE, transfer);

        transfer_nonblocking(curl);
    }
}

void Link_set_stat(Link* this_link, CURL *curl)
{
    long http_resp;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
    if (http_resp == HTTP_OK) {
        double cl = 0;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &cl);
        curl_easy_getinfo(curl, CURLINFO_FILETIME, &(this_link->time));
        if (cl == -1) {
            this_link->content_length = 0;
            this_link->type = LINK_DIR;
        } else {
            this_link->content_length = cl;
            this_link->type = LINK_FILE;
        }
    } else {
        fprintf(stderr, "Link_set_stat(): HTTP %ld.\n", http_resp);
        this_link->type = LINK_INVALID;
    }
}

static void LinkTable_fill(LinkTable *linktbl)
{
    Link *head_link = linktbl->links[0];
    for (int i = 0; i < linktbl->num; i++) {
        Link *this_link = linktbl->links[i];
        if (this_link->type) {
            char *url;
            url = path_append(head_link->f_url, this_link->linkname);
            strncpy(this_link->f_url, url, MAX_PATH_LEN);
            free(url);

            char *unescaped_linkname;
            unescaped_linkname = curl_easy_unescape(NULL, this_link->linkname, 0,
                                                 NULL);
            strncpy(this_link->linkname, unescaped_linkname, MAX_FILENAME_LEN);
            curl_free(unescaped_linkname);

            if (this_link->type == LINK_FILE && !(this_link->content_length)) {
                Link_get_stat(this_link);
            } else if (this_link->type == LINK_DIR) {
                this_link->time = head_link->time;
            }
        }
    }
    /* Block until the LinkTable is filled up */
    while (curl_multi_perform_once()) {
        usleep(1000);
    }
}

/**
 * \brief fill in the gaps in a link table
 */
static void LinkTable_gap_fill(LinkTable *linktbl)
{
    for (int i = 0; i < linktbl->num; i++) {
        if (linktbl->links[i]->type == LINK_INVALID) {
            Link_get_stat(linktbl->links[i]);
        }
    }

    /* Block until the gaps are filled */
    while (curl_multi_perform_once()) {
        usleep(1000);
    }
}


static void LinkTable_free(LinkTable *linktbl)
{
    for (int i = 0; i < linktbl->num; i++) {
        free(linktbl->links[i]);
    }
    free(linktbl->links);
    free(linktbl);
}

static void LinkTable_print(LinkTable *linktbl)
{
    fprintf(stderr, "--------------------------------------------\n");
    fprintf(stderr, " LinkTable %p for %s\n", linktbl,
            linktbl->links[0]->f_url);
    fprintf(stderr, "--------------------------------------------\n");
    for (int i = 0; i < linktbl->num; i++) {
        Link *this_link = linktbl->links[i];
        fprintf(stderr, "%d %c %lu %s %s\n",
                i,
                this_link->type,
                this_link->content_length,
                this_link->linkname,
                this_link->f_url
        );

    }
    fprintf(stderr, "--------------------------------------------\n");
}

LinkTable *LinkTable_new(const char *url)
{
    fprintf(stderr, "LinkTable_new(%s);\n", url);

    LinkTable *linktbl = calloc(1, sizeof(LinkTable));
    if (!linktbl) {
        fprintf(stderr, "LinkTable_new(): calloc failure!\n");
        exit(EXIT_FAILURE);
    }

    /* populate the base URL */
    LinkTable_add(linktbl, Link_new("/", LINK_HEAD));
    Link *head_link = linktbl->links[0];
    head_link->type = LINK_HEAD;
    strncpy(head_link->f_url, url, MAX_PATH_LEN);

    /* start downloading the base URL */
    CURL *curl = Link_to_curl(head_link);
    MemoryStruct buf;
    buf.size = 0;
    buf.memory = NULL;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);

    /* If we get HTTP 429, wait for 5 seconds before retry */
    volatile long http_resp = 0;
    do {
        transfer_blocking(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
        if (http_resp == HTTP_TOO_MANY_REQUESTS) {
            fprintf(stderr, "link.c: LinkTable_new(): URL: %s, HTTP 429, \
Too Many Requests\n", url);
            sleep(HTTP_429_WAIT);
        } else if (http_resp != HTTP_OK) {
            fprintf(stderr, "link.c: LinkTable_new(): cannot retrieve URL: %s, \
HTTP %ld\n", url, http_resp);
            LinkTable_free(linktbl);
            curl_easy_cleanup(curl);
            return NULL;
        };
    } while (http_resp != HTTP_OK);

    curl_easy_getinfo(curl, CURLINFO_FILETIME, &(head_link->time));
    curl_easy_cleanup(curl);

    /* Otherwise parsed the received data */
    GumboOutput* output = gumbo_parse(buf.memory);
    HTML_to_LinkTable(output->root, linktbl);
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    free(buf.memory);

    int skip_fill = 0;
    char *unescaped_path;
    unescaped_path = curl_easy_unescape(NULL, url + ROOT_LINK_OFFSET, 0, NULL);
    if (CACHE_SYSTEM_INIT) {
        CacheDir_create(unescaped_path);
        LinkTable *disk_linktbl;
        disk_linktbl = LinkTable_disk_open(unescaped_path);
        if (disk_linktbl) {
            /* Check if we need to update the link table */
            if (disk_linktbl->num == linktbl->num) {
                LinkTable_free(linktbl);
                linktbl = disk_linktbl;
                skip_fill = 1;
            } else {
                LinkTable_free(disk_linktbl);
            }
        }
    }

    if (!skip_fill) {
        /* Fill in the link table */
        LinkTable_fill(linktbl);
    } else {
        /* Fill in the holes in the link table */
        LinkTable_gap_fill(linktbl);
    }

    /* Save the link table */
    if (CACHE_SYSTEM_INIT) {
        LinkTable_disk_save(linktbl, unescaped_path);
    }

    curl_free(unescaped_path);

    LinkTable_print(linktbl);
    fprintf(stderr, "LinkTable_new(): returning LinkTable %p\n", linktbl);
    return linktbl;
}

static void LinkTable_disk_delete(const char *dirn)
{
    char *metadirn = path_append("cache/meta/", dirn);
    char *path = path_append(metadirn, ".LinkTable");
    if(unlink(path)) {
        fprintf(stderr, "LinkTable_disk_delete(): unlink(%s): %s\n", path,
                strerror(errno));
    }
    free(path);
    free(metadirn);
}

int LinkTable_disk_save(LinkTable *linktbl, const char *dirn)
{
    char *metadirn = path_append("cache/meta/", dirn);
    char *path = path_append(metadirn, ".LinkTable");
    FILE *fp = fopen(path, "w");
    free(metadirn);

    if (!fp) {
        fprintf(stderr, "LinkTable_disk_save(): fopen(%s): %s\n", path,
                strerror(errno));
        free(path);
        return -1;
    }
    free(path);

    fwrite(&linktbl->num, sizeof(int), 1, fp);
    for (int i = 0; i < linktbl->num; i++) {
        fwrite(linktbl->links[i]->linkname, sizeof(char), MAX_FILENAME_LEN, fp);
        fwrite(linktbl->links[i]->f_url, sizeof(char), MAX_PATH_LEN, fp);
        fwrite(&linktbl->links[i]->type, sizeof(LinkType), 1, fp);
        fwrite(&linktbl->links[i]->content_length, sizeof(size_t), 1, fp);
        fwrite(&linktbl->links[i]->time, sizeof(long), 1, fp);
    }

    int res = 0;

    if (ferror(fp)) {
        fprintf(stderr, "LinkTable_disk_save(): encountered ferror!\n");
        res = -1;
    }

    if (fclose(fp)) {
        fprintf(stderr,
                "LinkTable_disk_save(): cannot close the file pointer, %s\n",
                strerror(errno));
        res = -1;
    }

    return res;
}

LinkTable *LinkTable_disk_open(const char *dirn)
{
    char *metadirn = path_append("cache/meta/", dirn);
    char *path = path_append(metadirn, ".LinkTable");
    FILE *fp = fopen(path, "r");
    free(metadirn);

    if (!fp) {
        free(path);
        return NULL;
    }

    LinkTable *linktbl = calloc(1, sizeof(LinkTable));
    if (!linktbl) {
        fprintf(stderr, "LinkTable_disk_open(): calloc linktbl failed!\n");
        return NULL;
    }

    fread(&linktbl->num, sizeof(int), 1, fp);
    linktbl->links = calloc(linktbl->num, sizeof(Link *));
    for (int i = 0; i < linktbl->num; i++) {
        linktbl->links[i] = calloc(1, sizeof(Link));
        if (!linktbl->links[i]) {
            fprintf(stderr, "LinkTable_disk_open(): calloc links[i] failed!\n");
        }
        fread(linktbl->links[i]->linkname, sizeof(char), MAX_FILENAME_LEN, fp);
        fread(linktbl->links[i]->f_url, sizeof(char), MAX_PATH_LEN, fp);
        fread(&linktbl->links[i]->type, sizeof(LinkType), 1, fp);
        fread(&linktbl->links[i]->content_length, sizeof(size_t), 1, fp);
        fread(&linktbl->links[i]->time, sizeof(long), 1, fp);
        if (feof(fp)) {
            /* reached EOF */
            fprintf(stderr,
                    "LinkTable_disk_open(): reached EOF!\n");
            LinkTable_free(linktbl);
            LinkTable_disk_delete(dirn);
            return NULL;
        }
        if (ferror(fp)) {
            fprintf(stderr, "LinkTable_disk_open(): encountered ferror!\n");
            LinkTable_free(linktbl);
            LinkTable_disk_delete(dirn);
            return NULL;
        }
    }
    if (fclose(fp)) {
        fprintf(stderr,
                "LinkTable_disk_open(): cannot close the file pointer, %s\n",
                strerror(errno));
    }
    return linktbl;
}


LinkTable *path_to_Link_LinkTable_new(const char *path)
{
    Link *link = path_to_Link(path);
    if (!link->next_table) {
        link->next_table = LinkTable_new(link->f_url);
    }
    return link->next_table;
}

static Link *path_to_Link_recursive(char *path, LinkTable *linktbl)
{
    /* skip the leading '/' if it exists */
    if (*path == '/') {
        path++;
    }

    /* remove the last '/' if it exists */
    char *slash = &(path[strnlen(path, MAX_PATH_LEN) - 1]);
    if (*slash == '/') {
        *slash = '\0';
    }

    slash = strchr(path, '/');
    if ( slash == NULL ) {
        /* We cannot find another '/', we have reached the last level */
        for (int i = 1; i < linktbl->num; i++) {
            if (!strncmp(path, linktbl->links[i]->linkname, MAX_FILENAME_LEN)) {
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
         * add termination mark to the current string,
         * effective create two substrings
         */
        *slash = '\0';
        /* move the pointer past the '/' */
        char *next_path = slash + 1;
        for (int i = 1; i < linktbl->num; i++) {
            if (!strncmp(path, linktbl->links[i]->linkname, MAX_FILENAME_LEN)) {
                /* The next sub-directory exists */
                if (!linktbl->links[i]->next_table) {
                    linktbl->links[i]->next_table = LinkTable_new(
                        linktbl->links[i]->f_url);
                }
                return path_to_Link_recursive(
                    next_path, linktbl->links[i]->next_table);
            }
        }
    }
    return NULL;
}

Link *path_to_Link(const char *path)
{
    char *new_path = strndup(path, MAX_PATH_LEN);
    if (!new_path) {
        fprintf(stderr, "path_to_Link(): cannot allocate memory\n");
        exit(EXIT_FAILURE);
    }
    Link *link = path_to_Link_recursive(new_path, ROOT_LINK_TBL);
    free(new_path);
    return link;
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
    fprintf(stderr, "path_download(%s, %s);\n", path, range_str);

    MemoryStruct buf;
    buf.size = 0;
    buf.memory = NULL;

    CURL *curl = Link_to_curl(link);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&buf);
    curl_easy_setopt(curl, CURLOPT_RANGE, range_str);

    transfer_blocking(curl);

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
