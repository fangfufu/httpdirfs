#include "link.h"

#include "util.h"
#include "cache.h"
#include "network.h"
#include "sonic.h"
#include "log.h"

#include <gumbo.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STATUS_LEN 64

/*
 * ---------------- External variables -----------------------
 */
LinkTable *ROOT_LINK_TBL = NULL;
int ROOT_LINK_OFFSET = 0;

/**
 * \brief LinkTable generation priority lock
 * \details This allows LinkTable generation to be run exclusively. This
 * effectively gives LinkTable generation priority over file transfer.
 */
static pthread_mutex_t link_lock;

/**
 * \brief create a new Link
 */
static Link *Link_new(const char *linkname, LinkType type)
{
    Link *link = CALLOC(1, sizeof(Link));

    strncpy(link->linkname, linkname, MAX_FILENAME_LEN);
    link->type = type;

    /*
     * remove the '/' from linkname if it exists
     */
    char *c =
        &(link->linkname[strnlen(link->linkname, MAX_FILENAME_LEN) - 1]);
    if (*c == '/') {
        *c = '\0';
    }

    return link;
}

static CURL *Link_to_curl(Link *link)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        lprintf(fatal, "curl_easy_init() failed!\n");
    }
    /*
     * set up some basic curl stuff
     */
    CURLcode ret =
        curl_easy_setopt(curl, CURLOPT_USERAGENT, CONFIG.user_agent);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    /*
     * for following directories without the '/'
     */
    ret = curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 2);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_URL, link->f_url);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_SHARE, CURL_SHARE);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    ret =
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    if (CONFIG.insecure_tls) {
        ret = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        if (ret) {
            lprintf(error, "%s", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.log_type & libcurl_debug) {
        ret = curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        if (ret) {
            lprintf(error, "%s", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.http_username) {
        ret = curl_easy_setopt(curl, CURLOPT_USERNAME, CONFIG.http_username);
        if (ret) {
            lprintf(error, "%s", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.http_password) {
        ret = curl_easy_setopt(curl, CURLOPT_PASSWORD, CONFIG.http_password);
        if (ret) {
            lprintf(error, "%s", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.proxy) {
        ret = curl_easy_setopt(curl, CURLOPT_PROXY, CONFIG.proxy);
        if (ret) {
            lprintf(error, "%s", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.proxy_username) {
        ret = curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME,
                               CONFIG.proxy_username);
        if (ret) {
            lprintf(error, "%s", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.proxy_password) {
        ret = curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD,
                               CONFIG.proxy_password);
        if (ret) {
            lprintf(error, "%s", curl_easy_strerror(ret));
        }
    }

    return curl;
}

static void Link_req_file_stat(Link *this_link)
{
    CURL *curl = Link_to_curl(this_link);
    CURLcode ret = curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }

    /*
     * We need to put the variable on the heap, because otherwise the
     * variable gets popped from the stack as the function returns.
     *
     * It gets freed in curl_process_msgs();
     */
    TransferStruct *transfer = CALLOC(1, sizeof(TransferStruct));

    transfer->link = this_link;
    transfer->type = FILESTAT;
    ret = curl_easy_setopt(curl, CURLOPT_PRIVATE, transfer);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }

    transfer_nonblocking(curl);
}

/**
 * \brief Fill in the uninitialised entries in a link table
 * \details Try and get the stats for each link in the link table. This will get
 * repeated until the uninitialised entry count drop to zero.
 */
static void LinkTable_uninitialised_fill(LinkTable *linktbl)
{
    int u;
    char s[STATUS_LEN];
    lprintf(debug, " ... ");
    do {
        u = 0;
        for (int i = 0; i < linktbl->num; i++) {
            Link *this_link = linktbl->links[i];
            if (this_link->type == LINK_UNINITIALISED_FILE) {
                Link_req_file_stat(linktbl->links[i]);
                u++;
            }
        }
        /*
         * Block until the gaps are filled
         */
        int n = curl_multi_perform_once();
        int i = 0;
        int j = 0;
        while ((i = curl_multi_perform_once())) {
            if (CONFIG.log_type & debug) {
                if (j) {
                    erase_string(stderr, STATUS_LEN, s);
                }
                snprintf(s, STATUS_LEN, "%d / %d", n - i, n);
                fprintf(stderr, "%s", s);
                j++;
            }
        }
    } while (u);
    if (CONFIG.log_type & debug) {
        erase_string(stderr, STATUS_LEN, s);
        fprintf(stderr, "... Done!\n");
    }
}

/**
 * \brief Create the root linktable for single file mode
 */
static LinkTable *single_LinkTable_new(const char *url)
{
    char *ptr = strrchr(url, '/') + 1;
    LinkTable *linktbl = LinkTable_alloc(url);
    Link *link = Link_new(ptr, LINK_UNINITIALISED_FILE);
    strncpy(link->f_url, url, MAX_FILENAME_LEN);
    LinkTable_add(linktbl, link);
    LinkTable_uninitialised_fill(linktbl);
    LinkTable_print(linktbl);
    return linktbl;
}

LinkTable *LinkSystem_init(const char *raw_url)
{
    if (pthread_mutex_init(&link_lock, NULL)) {
        lprintf(error, "link_lock initialisation failed!\n");
    }
    /*
     * Remove excess '/' if it is there
     */
    char *url = strdup(raw_url);
    int url_len = strnlen(url, MAX_PATH_LEN) - 1;
    if (url[url_len] == '/') {
        url[url_len] = '\0';
    }
    /*
     * --------- Set the length of the root link -----------
     */
    /*
     * This is where the '/' should be
     */
    ROOT_LINK_OFFSET = strnlen(url, MAX_PATH_LEN);

    /*
     * --------------------- Enable cache system --------------------
     */
    if (CONFIG.cache_enabled) {
        if (CONFIG.cache_dir) {
            CacheSystem_init(CONFIG.cache_dir, 0);
        } else {
            CacheSystem_init(url, 1);
        }
    }

    /*
     * ----------- Create the root link table --------------
     */
    if (CONFIG.mode == NORMAL) {
        ROOT_LINK_TBL = LinkTable_new(url);
    } else if (CONFIG.mode == SINGLE) {
        ROOT_LINK_TBL = single_LinkTable_new(url);
    } else if (CONFIG.mode == SONIC) {
        sonic_config_init(url, CONFIG.sonic_username,
                          CONFIG.sonic_password);
        if (!CONFIG.sonic_id3) {
            ROOT_LINK_TBL = sonic_LinkTable_new_index("0");
        } else {
            ROOT_LINK_TBL = sonic_LinkTable_new_id3(0, "0");
        }
    } else {
        lprintf(fatal, "Invalid CONFIG.mode\n");
    }
    FREE(url);
    return ROOT_LINK_TBL;
}

void LinkTable_add(LinkTable *linktbl, Link *link)
{
    linktbl->num++;
    linktbl->links =
        realloc(linktbl->links, linktbl->num * sizeof(Link *));
    if (!linktbl->links) {
        lprintf(fatal, "realloc() failure!\n");
    }
    linktbl->links[linktbl->num - 1] = link;
}

static LinkType linkname_to_LinkType(const char *linkname)
{
    /*
     * The link name has to start with alphanumerical character
     */
    if (!isalnum(linkname[0]) && (linkname[0] != '%')) {
        return LINK_INVALID;
    }

    /*
     * Check for stray '/' - Linkname should not have '/'
     */
    char *slash = strchr(linkname, '/');
    if (slash) {
        int linkname_len = strnlen(linkname, MAX_FILENAME_LEN) - 1;
        if (slash - linkname != linkname_len) {
            return LINK_INVALID;
        }
    }

    if (linkname[strnlen(linkname, MAX_FILENAME_LEN) - 1] == '/') {
        return LINK_DIR;
    }

    return LINK_UNINITIALISED_FILE;
}

/**
 * \brief check if two link names are equal, after taking the '/' into account.
 */
static int linknames_equal(char *linkname, const char *linkname_new)
{
    if (!strncmp(linkname, linkname_new, MAX_FILENAME_LEN)) {
        return 1;
    }

    /*
     * check if the link names differ by a single '/'
     */
    if (!strncmp
            (linkname, linkname_new, strnlen(linkname, MAX_FILENAME_LEN))) {
        size_t linkname_new_len = strnlen(linkname_new, MAX_FILENAME_LEN);
        if ((linkname_new_len - strnlen(linkname, MAX_FILENAME_LEN) == 1)
                && (linkname_new[linkname_new_len - 1] == '/')) {
            return 1;
        }
    }
    return 0;
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
    GumboAttribute *href;
    if (node->v.element.tag == GUMBO_TAG_A &&
            (href =
                 gumbo_get_attribute(&node->v.element.attributes, "href"))) {
        /*
         * if it is valid, copy the link onto the heap
         */
        LinkType type = linkname_to_LinkType(href->value);
        /*
         * We also check if the link being added is the same as the last link.
         * This is to prevent duplicated link, if an Apache server has the
         * IconsAreLinks option.
         */
        size_t comp_len = strnlen(href->value, MAX_FILENAME_LEN);
        if (type == LINK_DIR) {
            comp_len--;
        }
        if (((type == LINK_DIR) || (type == LINK_UNINITIALISED_FILE)) &&
                !linknames_equal(linktbl->links[linktbl->num - 1]->linkname,
                                 href->value)) {
            LinkTable_add(linktbl, Link_new(href->value, type));
        }
    }
    /*
     * Note the recursive call, lol.
     */
    GumboVector *children = &node->v.element.children;
    for (size_t i = 0; i < children->length; ++i) {
        HTML_to_LinkTable((GumboNode *) children->data[i], linktbl);
    }
    return;
}

void Link_set_file_stat(Link *this_link, CURL *curl)
{
    long http_resp;
    CURLcode ret =
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    if (http_resp == HTTP_OK) {
        double cl = 0;
        ret =
            curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &cl);
        if (ret) {
            lprintf(error, "%s", curl_easy_strerror(ret));
        }
        ret =
            curl_easy_getinfo(curl, CURLINFO_FILETIME, &(this_link->time));
        if (ret) {
            lprintf(error, "%s", curl_easy_strerror(ret));
        }
        if (cl <= 0) {
            this_link->type = LINK_INVALID;
        } else {
            this_link->type = LINK_FILE;
            this_link->content_length = cl;
        }
    } else {
        lprintf(warning, "HTTP %ld", http_resp);
        if (HTTP_temp_failure(http_resp)) {
            lprintf(warning, ", retrying later.\n");
        } else {
            this_link->type = LINK_INVALID;
            lprintf(warning, ".\n");
        }
    }
}

static void LinkTable_fill(LinkTable *linktbl)
{
    Link *head_link = linktbl->links[0];
    for (int i = 1; i < linktbl->num; i++) {
        Link *this_link = linktbl->links[i];
        char *url;
        url = path_append(head_link->f_url, this_link->linkname);
        strncpy(this_link->f_url, url, MAX_PATH_LEN);
        FREE(url);
        char *unescaped_linkname;
        CURL *c = curl_easy_init();
        unescaped_linkname = curl_easy_unescape(c, this_link->linkname,
                                                0, NULL);
        strncpy(this_link->linkname, unescaped_linkname, MAX_FILENAME_LEN);
        curl_free(unescaped_linkname);
        curl_easy_cleanup(c);
    }
    LinkTable_uninitialised_fill(linktbl);
}

/**
 * \brief Reset invalid links in the link table
 */
static void LinkTable_invalid_reset(LinkTable *linktbl)
{
    int j = 0;
    for (int i = 0; i < linktbl->num; i++) {
        Link *this_link = linktbl->links[i];
        if (this_link->type == LINK_INVALID) {
            this_link->type = LINK_UNINITIALISED_FILE;
            j++;
        }
    }
    lprintf(debug, "%d invalid links\n", j);
}

void LinkTable_free(LinkTable *linktbl)
{
    for (int i = 0; i < linktbl->num; i++) {
        FREE(linktbl->links[i]);
    }
    FREE(linktbl->links);
    FREE(linktbl);
}

void LinkTable_print(LinkTable *linktbl)
{
    if (CONFIG.log_type & info) {
        int j = 0;
        lprintf(info, "--------------------------------------------\n");
        lprintf(info, " LinkTable %p for %s\n", linktbl,
                linktbl->links[0]->f_url);
        lprintf(info, "--------------------------------------------\n");
        for (int i = 0; i < linktbl->num; i++) {
            Link *this_link = linktbl->links[i];
            lprintf(info, "%d %c %lu %s %s\n",
                    i,
                    this_link->type,
                    this_link->content_length,
                    this_link->linkname, this_link->f_url);
            if ((this_link->type != LINK_FILE)
                    && (this_link->type != LINK_DIR)
                    && (this_link->type != LINK_HEAD)) {
                j++;
            }
        }
        lprintf(info, "--------------------------------------------\n");
        lprintf(info, " Invalid link count: %d\n", j);
        lprintf(info, "--------------------------------------------\n");
    }
}

LinkTable *LinkTable_alloc(const char *url)
{
    LinkTable *linktbl = CALLOC(1, sizeof(LinkTable));

    /*
     * populate the base URL
     */
    Link *head_link = Link_new("/", LINK_HEAD);
    LinkTable_add(linktbl, head_link);
    strncpy(head_link->f_url, url, MAX_PATH_LEN);
    assert(linktbl->num == 1);
    return linktbl;
}

LinkTable *LinkTable_new(const char *url)
{
    LinkTable *linktbl = LinkTable_alloc(url);

    /*
     * start downloading the base URL
     */
    TransferStruct ts = Link_download_full(linktbl->links[0]);
    if (ts.curr_size == 0) {
        LinkTable_free(linktbl);
        return NULL;
    }

    /*
     * Otherwise parsed the received data
     */
    GumboOutput *output = gumbo_parse(ts.data);
    HTML_to_LinkTable(output->root, linktbl);
    gumbo_destroy_output(&kGumboDefaultOptions, output);
    FREE(ts.data);

    int skip_fill = 0;
    char *unescaped_path;
    CURL *c = curl_easy_init();
    unescaped_path =
        curl_easy_unescape(c, url + ROOT_LINK_OFFSET, 0, NULL);
    if (CACHE_SYSTEM_INIT) {
        CacheDir_create(unescaped_path);
        LinkTable *disk_linktbl;
        disk_linktbl = LinkTable_disk_open(unescaped_path);
        if (disk_linktbl) {
            /*
             * Check if we need to update the link table
             */
            lprintf(debug,
                    "disk_linktbl->num: %d, linktbl->num: %d\n",
                    disk_linktbl->num, linktbl->num);
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
        /*
         * Fill in the link table
         */
        LinkTable_fill(linktbl);
    } else {
        /*
         * Fill in the holes in the link table
         */
        LinkTable_invalid_reset(linktbl);
        LinkTable_uninitialised_fill(linktbl);
    }

    /*
     * Save the link table
     */
    if (CACHE_SYSTEM_INIT) {
        if (LinkTable_disk_save(linktbl, unescaped_path)) {
            lprintf(error, "Failed to save the LinkTable!\n");
        }
    }

    curl_free(unescaped_path);
    curl_easy_cleanup(c);

    LinkTable_print(linktbl);

    return linktbl;
}

static void LinkTable_disk_delete(const char *dirn)
{
    char *metadirn = path_append(META_DIR, dirn);
    char *path;
    path = path_append(metadirn, "/.LinkTable");
    if (unlink(path)) {
        lprintf(error, "unlink(%s): %s\n", path, strerror(errno));
    }
    FREE(path);
    FREE(metadirn);
}

int LinkTable_disk_save(LinkTable *linktbl, const char *dirn)
{
    char *metadirn = path_append(META_DIR, dirn);
    char *path;
    path = path_append(metadirn, "/.LinkTable");
    FILE *fp = fopen(path, "w");
    FREE(metadirn);

    if (!fp) {
        lprintf(error, "fopen(%s): %s\n", path, strerror(errno));
        FREE(path);
        return -1;
    }
    FREE(path);

    fwrite(&linktbl->num, sizeof(int), 1, fp);
    for (int i = 0; i < linktbl->num; i++) {
        fwrite(linktbl->links[i]->linkname, sizeof(char),
               MAX_FILENAME_LEN, fp);
        fwrite(linktbl->links[i]->f_url, sizeof(char), MAX_PATH_LEN, fp);
        fwrite(&linktbl->links[i]->type, sizeof(LinkType), 1, fp);
        fwrite(&linktbl->links[i]->content_length, sizeof(size_t), 1, fp);
        fwrite(&linktbl->links[i]->time, sizeof(long), 1, fp);
    }

    int res = 0;

    if (ferror(fp)) {
        lprintf(error, "encountered ferror!\n");
        res = -1;
    }

    if (fclose(fp)) {
        lprintf(error,
                "cannot close the file pointer, %s\n", strerror(errno));
        res = -1;
    }

    return res;
}

LinkTable *LinkTable_disk_open(const char *dirn)
{
    char *metadirn = path_append(META_DIR, dirn);
    char *path;
    if (metadirn[strnlen(metadirn, MAX_PATH_LEN)] == '/') {
        path = path_append(metadirn, ".LinkTable");
    } else {
        path = path_append(metadirn, "/.LinkTable");
    }
    FILE *fp = fopen(path, "r");
    FREE(metadirn);

    if (!fp) {
        FREE(path);
        return NULL;
    }

    LinkTable *linktbl = CALLOC(1, sizeof(LinkTable));

    fread(&linktbl->num, sizeof(int), 1, fp);
    linktbl->links = CALLOC(linktbl->num, sizeof(Link *));
    for (int i = 0; i < linktbl->num; i++) {
        linktbl->links[i] = CALLOC(1, sizeof(Link));
        fread(linktbl->links[i]->linkname, sizeof(char),
              MAX_FILENAME_LEN, fp);
        fread(linktbl->links[i]->f_url, sizeof(char), MAX_PATH_LEN, fp);
        fread(&linktbl->links[i]->type, sizeof(LinkType), 1, fp);
        fread(&linktbl->links[i]->content_length, sizeof(size_t), 1, fp);
        fread(&linktbl->links[i]->time, sizeof(long), 1, fp);
        if (feof(fp)) {
            /*
             * reached EOF
             */
            lprintf(error, "reached EOF!\n");
            LinkTable_free(linktbl);
            LinkTable_disk_delete(dirn);
            return NULL;
        }
        if (ferror(fp)) {
            lprintf(error, "encountered ferror!\n");
            LinkTable_free(linktbl);
            LinkTable_disk_delete(dirn);
            return NULL;
        }
    }
    if (fclose(fp)) {
        lprintf(error,
                "cannot close the file pointer, %s\n", strerror(errno));
    }
    return linktbl;
}

LinkTable *path_to_Link_LinkTable_new(const char *path)
{
    Link *link = path_to_Link(path);
    LinkTable *next_table = link->next_table;
    if (!next_table) {
        if (CONFIG.mode == NORMAL) {
            next_table = LinkTable_new(link->f_url);
        } else if (CONFIG.mode == SONIC) {
            if (!CONFIG.sonic_id3) {
                next_table = sonic_LinkTable_new_index(link->sonic.id);
            } else {
                next_table =
                    sonic_LinkTable_new_id3(link->sonic.depth,
                                            link->sonic.id);
            }
        } else {
            lprintf(fatal, "Invalid CONFIG.mode\n");
        }
    }
    link->next_table = next_table;
    return next_table;
}

static Link *path_to_Link_recursive(char *path, LinkTable *linktbl)
{
    /*
     * skip the leading '/' if it exists
     */
    if (*path == '/') {
        path++;
    }

    /*
     * remove the last '/' if it exists
     */
    char *slash = &(path[strnlen(path, MAX_PATH_LEN) - 1]);
    if (*slash == '/') {
        *slash = '\0';
    }

    slash = strchr(path, '/');
    if (slash == NULL) {
        /*
         * We cannot find another '/', we have reached the last level
         */
        for (int i = 1; i < linktbl->num; i++) {
            if (!strncmp
                    (path, linktbl->links[i]->linkname, MAX_FILENAME_LEN)) {
                /*
                 * We found our link
                 */
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
        /*
         * move the pointer past the '/'
         */
        char *next_path = slash + 1;
        for (int i = 1; i < linktbl->num; i++) {
            if (!strncmp
                    (path, linktbl->links[i]->linkname, MAX_FILENAME_LEN)) {
                /*
                 * The next sub-directory exists
                 */
                LinkTable *next_table = linktbl->links[i]->next_table;
                if (!next_table) {
                    if (CONFIG.mode == NORMAL) {
                        next_table =
                            LinkTable_new(linktbl->links[i]->f_url);
                    } else if (CONFIG.mode == SONIC) {
                        if (!CONFIG.sonic_id3) {
                            next_table =
                                sonic_LinkTable_new_index
                                (linktbl->links[i]->sonic.id);
                        } else {
                            next_table =
                                sonic_LinkTable_new_id3
                                (linktbl->links
                                 [i]->sonic.depth,
                                 linktbl->links[i]->sonic.id);
                        }
                    } else {
                        lprintf(fatal, "Invalid CONFIG.mode\n");
                    }
                }
                linktbl->links[i]->next_table = next_table;
                return path_to_Link_recursive(next_path, next_table);
            }
        }
    }
    return NULL;
}

Link *path_to_Link(const char *path)
{
    lprintf(link_lock_debug,
            "thread %x: locking link_lock;\n", pthread_self());

    PTHREAD_MUTEX_LOCK(&link_lock);
    char *new_path = strndup(path, MAX_PATH_LEN);
    if (!new_path) {
        lprintf(fatal, "cannot allocate memory\n");
    }
    Link *link = path_to_Link_recursive(new_path, ROOT_LINK_TBL);
    FREE(new_path);

    lprintf(link_lock_debug,
            "thread %x: unlocking link_lock;\n", pthread_self());
    PTHREAD_MUTEX_UNLOCK(&link_lock);
    return link;
}

TransferStruct Link_download_full(Link *link)
{
    char *url = link->f_url;
    CURL *curl = Link_to_curl(link);

    TransferStruct ts;
    ts.curr_size = 0;
    ts.data = NULL;
    ts.type = DATA;
    ts.transferring = 1;

    CURLcode ret = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) &ts);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_PRIVATE, (void *) &ts);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }

    /*
     * If we get temporary HTTP failure, wait for 5 seconds before retry
     */
    long http_resp = 0;
    do {
        transfer_blocking(curl);
        ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
        if (ret) {
            lprintf(error, "%s", curl_easy_strerror(ret));
        }
        if (HTTP_temp_failure(http_resp)) {
            lprintf(warning,
                    "URL: %s, HTTP %ld, retrying later.\n",
                    url, http_resp);
            sleep(CONFIG.http_wait_sec);
        } else if (http_resp != HTTP_OK) {
            lprintf(warning,
                    "cannot retrieve URL: %s, HTTP %ld\n", url, http_resp);
            ts.curr_size = 0;
            FREE(ts.data);
            curl_easy_cleanup(curl);
            return ts;
        }
    } while (HTTP_temp_failure(http_resp));

    ret = curl_easy_getinfo(curl, CURLINFO_FILETIME, &(link->time));
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    curl_easy_cleanup(curl);
    return ts;
}

static CURL *Link_download_curl_setup(Link *link, size_t req_size, off_t offset,
                                      TransferStruct *header,
                                      TransferStruct *ts)
{
    if (!link) {
        lprintf(fatal, "Invalid supplied\n");
    }

    size_t start = offset;
    size_t end = start + req_size;

    char range_str[64];
    snprintf(range_str, sizeof(range_str), "%lu-%lu", start, end);
    lprintf(debug, "%s: %s\n", link->linkname, range_str);

    CURL *curl = Link_to_curl(link);
    CURLcode ret =
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *) header);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) ts);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_PRIVATE, (void *) ts);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_RANGE, range_str);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }

    return curl;
}

static curl_off_t Link_download_cleanup(CURL *curl, TransferStruct *header)
{
    /*
     * Check for range seek support
     */
    if (!CONFIG.no_range_check) {
        if (!strcasestr((header->data), "Accept-Ranges: bytes")) {
            fprintf(stderr, "This web server does not support HTTP \
range requests\n");
            exit(EXIT_FAILURE);
        }
    }

    FREE(header->data);

    long http_resp;
    CURLcode ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    if (!((http_resp != HTTP_OK) ||
            (http_resp != HTTP_PARTIAL_CONTENT) ||
            (http_resp != HTTP_RANGE_NOT_SATISFIABLE))) {
        char *url;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);
        lprintf(warning, "Could not download %s, HTTP %ld\n", url, http_resp);
        return -ENOENT;
    }

    curl_off_t recv;
    ret = curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &recv);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }

    curl_easy_cleanup(curl);

    return recv;
}

long Link_download(Link *link, char *output_buf, size_t req_size, off_t offset)
{
    TransferStruct ts;
    ts.curr_size = 0;
    ts.data = NULL;
    ts.type = DATA;
    ts.transferring = 1;

    TransferStruct header;
    header.curr_size = 0;
    header.data = NULL;

    CURL *curl = Link_download_curl_setup(link, req_size, offset, &header, &ts);

    transfer_blocking(curl);

    curl_off_t recv = Link_download_cleanup(curl, &header);

    /* The extra 1 byte is probably for '\0' */
    if (recv - 1 == (long int) req_size) {
        recv--;
    } else if (offset + req_size < link->content_length) {
        lprintf(error, "req_size: %lu, recv: %ld\n", req_size, recv);
    }

    memmove(output_buf, ts.data, recv);
    FREE(ts.data);

    return recv;
}

long path_download(const char *path, char *output_buf, size_t req_size,
                   off_t offset)
{
    if (!path) {
        lprintf(fatal, "NULL path supplied\n");
    }

    Link *link;
    link = path_to_Link(path);
    if (!link) {
        return -ENOENT;
    }

    return Link_download(link, output_buf, req_size, offset);
}
