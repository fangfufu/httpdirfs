#include "link.h"

#include "log.h"
#include "memcache.h"
#include "util.h"

#include <gumbo.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>

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
static void make_link_relative(const char *page_url, char *link_url);

/**
 * \brief create a new Link
 */
static Link *Link_new(const char *linkname, LinkType type)
{
    Link *link = CALLOC(1, sizeof(Link));

    strncpy(link->linkname, linkname, MAX_FILENAME_LEN);
    strncpy(link->linkpath, linkname, MAX_FILENAME_LEN);
    link->type = type;

    /*
     * remove the '/' from linkname if it exists
     */
    char *c = &(link->linkname[strnlen(link->linkname, MAX_FILENAME_LEN) - 1]);
    if (*c == '/') {
        *c = '\0';
    }

    return link;
}

static CURL *Link_to_curl(Link *link)
{
    lprintf(debug, "%s\n", link->f_url);
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
    if (CONFIG.cafile) {
        /*
         * Having been given a certificate file, disable any search directory
         * built into libcurl, so that we exclusively use the explicitly given
         * certificate(s).
         *
         * If we ever add a CAPATH option, we should do the mirror for CAINFO,
         * too: disable both and then enable whichever one(s) were given.
         */
        ret = curl_easy_setopt(curl, CURLOPT_CAPATH, NULL);
        if (ret) {
            lprintf(error, "%s", curl_easy_strerror(ret));
        }

        ret = curl_easy_setopt(curl, CURLOPT_CAINFO, CONFIG.cafile);
        if (ret) {
            lprintf(error, "%s", curl_easy_strerror(ret));
        }
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

    if (CONFIG.http_headers) {
        ret =
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, CONFIG.http_headers);
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

    if (CONFIG.proxy_cafile) {
        /* See CONFIG.cafile above */
        ret = curl_easy_setopt(curl, CURLOPT_PROXY_CAPATH, NULL);
        if (ret) {
            lprintf(error, "%s", curl_easy_strerror(ret));
        }

        ret = curl_easy_setopt(curl, CURLOPT_PROXY_CAINFO,
                               CONFIG.proxy_cafile);
        if (ret) {
            lprintf(error, "%s", curl_easy_strerror(ret));
        }
    }

    return curl;
}

static void Link_req_file_stat(Link *this_link)
{
    lprintf(debug, "%s\n", this_link->f_url);
    CURL *curl = Link_to_curl(this_link);
    CURLcode ret = curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
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
    char *orig_ptr = strrchr(url, '/') + 1;
    char *ptr = curl_easy_unescape(NULL, orig_ptr, 0, NULL);
    LinkTable *linktbl = LinkTable_alloc(url);
    Link *link = Link_new(ptr, LINK_UNINITIALISED_FILE);
    strncpy(link->f_url, url, MAX_FILENAME_LEN);
    LinkTable_add(linktbl, link);
    LinkTable_uninitialised_fill(linktbl);
    LinkTable_print(linktbl);
    return linktbl;
}

LinkTable *LinkSystem_init(const char *url)
{
    PTHREAD_MUTEX_INIT(&link_lock, NULL);
    int url_len = strnlen(url, MAX_PATH_LEN) - 1;
    /*
     * --------- Set the length of the root link -----------
     */
    /*
     * This is where the '/' should be
     */
    ROOT_LINK_OFFSET = strnlen(url, MAX_PATH_LEN) -
                       ((url[url_len] == '/') ? 1 : 0);

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
    lprintf(debug, "linkname: %s\n", linkname);
    if (linkname[0] == '\0' || linkname[0] == '/') {
        return LINK_INVALID;
    }

    /* Now allow all printable characters */
    for (int i = 0; linkname[i] != '\0'; i++) {
        char c = linkname[i];
        if (!isprint(c)) {
            return LINK_INVALID;
        }
    }

    /* The linkname must not contain '/' in the middle. */
    char *slash = strchr(linkname, '/');
    if (slash) {
        int linkname_len = strnlen(linkname, MAX_FILENAME_LEN) - 1;
        if (slash - linkname != linkname_len) {
            return LINK_INVALID;
        }
    }

    /* '/' must be at the end to be a valid directory name */
    if (linkname[strnlen(linkname, MAX_FILENAME_LEN) - 1] == '/') {
        return LINK_DIR;
    }

    return LINK_UNINITIALISED_FILE;
}

/**
 * \brief check if two link names are equal, after taking the '/' into account.
 */
static int linknames_equal(const char *str_a, const char *str_b)
{
    size_t len_a = strnlen(str_a, MAX_FILENAME_LEN);
    size_t len_b = strnlen(str_b, MAX_FILENAME_LEN);
    size_t max_len = MAX(len_a, len_b);
    size_t comp_len = MIN(len_a, len_b);
    int identical = 0;

    /* The length of the strings differ by more than 1 character. */
    if (max_len - comp_len > 1) {
        goto end;
    }

    /* Assuming that the shorter string has a non-zero length */
    if (comp_len) {
        /* Assuming that the common parts of the strings are the same */
        if(!strncmp(str_a, str_b, comp_len)) {
            /* If the lengths are equal, they are identical */
            if (len_a == len_b) {
                identical = 1;
            } else {
                /* Otherwise the last character of the longer string should be '/' */
                const char *longer_str = len_a > len_b ? str_a : str_b;
                identical = (longer_str[comp_len] == '/');
            }
        }
    }

end:
    lprintf(debug, "linknames comparison: a: %s, b: %s, max_len: %d %s\n",
            str_a, str_b, comp_len, identical ? ", identical!" : "");
    return identical;
}

/**
 * Shamelessly copied and pasted from:
 * https://github.com/google/gumbo-parser/blob/master/examples/find_links.cc
 */
static void HTML_to_LinkTable(const char *url, GumboNode *node,
                              LinkTable *linktbl)
{
    if (node->type != GUMBO_NODE_ELEMENT) {
        return;
    }
    GumboAttribute *href;
    if (node->v.element.tag == GUMBO_TAG_A &&
            (href =
                 gumbo_get_attribute(&node->v.element.attributes, "href"))) {
        char *relative_url = (char *) href->value;
        make_link_relative(url, relative_url);

        /* if it is valid, copy the link onto the heap */
        LinkType type = linkname_to_LinkType(relative_url);

        /* Check if the new link is a duplicate */
        if ((type == LINK_DIR) || (type == LINK_UNINITIALISED_FILE)) {
            int identical_link_found = 0;
            for (int i = 0; i < linktbl->num; i++) {
                if (linknames_equal(relative_url, linktbl->links[i]->linkname)) {
                    identical_link_found = 1;
                    break;
                }
            }
            if (!identical_link_found) {
                LinkTable_add(linktbl, Link_new(relative_url, type));
            }
        }
    }

    /* Note the recursive call */
    GumboVector *children = &node->v.element.children;
    for (size_t i = 0; i < children->length; ++i) {
        HTML_to_LinkTable(url, (GumboNode *) children->data[i], linktbl);
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
        curl_off_t cl = 0;
        ret =
            curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
        if (ret) {
            lprintf(error, "%s", curl_easy_strerror(ret));
        }
        ret =
            curl_easy_getinfo(curl, CURLINFO_FILETIME, &(this_link->time));
        if (ret) {
            lprintf(error, "%s", curl_easy_strerror(ret));
        }
        if (cl < 0) {
            this_link->type = LINK_INVALID;
        } else if (cl == 0 && CONFIG.zero_len_is_dir) {
            this_link->type = LINK_DIR;
        } else {
            this_link->type = LINK_FILE;
            this_link->content_length = cl;
        }
    } else {
        lprintf(warning, "HTTP %ld\n", http_resp);
        if (HTTP_temp_failure(http_resp) || CONFIG.invalid_refresh) {
            lprintf(warning, ", retrying later.\n");
        } else {
            this_link->type = LINK_INVALID;
        }
    }
}

static void LinkTable_fill(LinkTable *linktbl)
{
    Link *head_link = linktbl->links[0];
    lprintf(debug, "Filling %s\n", head_link->f_url);
    for (int i = 1; i < linktbl->num; i++) {
        Link *this_link = linktbl->links[i];
        /* Some web sites use characters in their href attributes that really
           shouldn't be in their href attributes, most commonly spaces. And
           some web sites _do_ properly encode their href attributes. So we
           first unescape the link path, and then we escape it, so that curl
           will definitely be happy with it (e.g., curl won't accept URLs with
           spaces in them!). If we only escaped it, and there were already
           encoded characters in it, then that would break the link. */
        char *unescaped_path = curl_easy_unescape(NULL, this_link->linkpath, 0,
                               NULL);
        char *escaped_path = curl_easy_escape(NULL, unescaped_path, 0);
        curl_free(unescaped_path);
        /* Our code does the wrong thing if there's a trailing slash that's been
           replaced with %2F, which curl_easy_escape does, God bless it, so if
           it did that then let's put it back. */
        int escaped_len = strlen(escaped_path);
        if (escaped_len >= 3 && !strcmp(escaped_path + escaped_len - 3, "%2F"))
            strcpy(escaped_path + escaped_len - 3, "/");
        char *url = path_append(head_link->f_url, escaped_path);
        curl_free(escaped_path);
        strncpy(this_link->f_url, url, MAX_PATH_LEN);
        FREE(url);
        char *unescaped_linkname;
        unescaped_linkname = curl_easy_unescape(NULL, this_link->linkname,
                                                0, NULL);
        strncpy(this_link->linkname, unescaped_linkname, MAX_FILENAME_LEN);
        curl_free(unescaped_linkname);
    }
    LinkTable_uninitialised_fill(linktbl);
}

void LinkTable_free(LinkTable *linktbl)
{
    if (linktbl) {
        for (int i = 0; i < linktbl->num; i++) {
            LinkTable_free(linktbl->links[i]->next_table);
            FREE(linktbl->links[i]);
        }
        FREE(linktbl->links);
        FREE(linktbl);
    }
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
    linktbl->num = 0;
    linktbl->index_time = 0;
    linktbl->links = NULL;


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
    char *unescaped_path;
    unescaped_path =
        curl_easy_unescape(NULL, url + ROOT_LINK_OFFSET, 0, NULL);
    LinkTable *linktbl = NULL;

    /*
     * Attempt to load the LinkTable from the disk.
     */
    if (CACHE_SYSTEM_INIT) {
        CacheDir_create(unescaped_path);
        LinkTable *disk_linktbl;

        disk_linktbl = LinkTable_disk_open(unescaped_path);
        if (disk_linktbl) {
            /*
             * Check if the LinkTable needs to be refreshed based on timeout.
             */
            time_t time_now = time(NULL);
            if (time_now - disk_linktbl->index_time  > CONFIG.refresh_timeout) {
                lprintf(info, "time_now: %d, index_time: %d\n", time_now,
                        disk_linktbl->index_time);
                lprintf(info, "diff: %d, limit: %d\n",
                        time_now - disk_linktbl->index_time,
                        CONFIG.refresh_timeout);
                LinkTable_free(disk_linktbl);
            } else {
                linktbl = disk_linktbl;
            }
        }
    }

    /*
     * Download a new LinkTable because we didn't manange to load it from the
     * disk
     */
    if (!linktbl) {
        linktbl = LinkTable_alloc(url);
        linktbl->index_time = time(NULL);
        lprintf(debug, "linktbl->index_time: %d\n", linktbl->index_time);

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
        HTML_to_LinkTable(url, output->root, linktbl);
        gumbo_destroy_output(&kGumboDefaultOptions, output);
        FREE(ts.data);

        LinkTable_fill(linktbl);

        /*
        * Save the link table
        */
        if (CACHE_SYSTEM_INIT) {
            if (LinkTable_disk_save(linktbl, unescaped_path)) {
                lprintf(error, "Failed to save the LinkTable!\n");
            }
        }
    }

    static unsigned long long i = 0;
    lprintf(debug, "Calling LinkTable_new for the %d time!\n", i);
    i++;

    free(unescaped_path);
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

/* This is necessary to get the compiler on some platforms to stop
   complaining about the fact that we're not using the return value of
   fread, when we know we aren't and that's fine. */
static inline void ignore_value(int i)
{
    (void) i;
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

    lprintf(debug, "linktbl->index_time: %d\n", linktbl->index_time);
    if (fwrite(&linktbl->num, sizeof(int), 1, fp) != 1 ||
            fwrite(&linktbl->index_time, sizeof(time_t), 1, fp) != 1) {
        lprintf(error, "Failed to save the header of %s!\n", path);
    }
    FREE(path);
    for (int i = 0; i < linktbl->num; i++) {
        ignore_value(fwrite(linktbl->links[i]->linkname, sizeof(char),
                            MAX_FILENAME_LEN, fp));
        ignore_value(fwrite(linktbl->links[i]->f_url, sizeof(char), MAX_PATH_LEN, fp));
        ignore_value(fwrite(&linktbl->links[i]->type, sizeof(LinkType), 1, fp));
        ignore_value(fwrite(&linktbl->links[i]->content_length, sizeof(size_t), 1, fp));
        ignore_value(fwrite(&linktbl->links[i]->time, sizeof(long), 1, fp));
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
        lprintf(debug, "Linktable at %s does not exist.", path);
        FREE(path);
        return NULL;
    }

    LinkTable *linktbl = CALLOC(1, sizeof(LinkTable));
    if (fread(&linktbl->num, sizeof(int), 1, fp) != 1 ||
            fread(&linktbl->index_time, sizeof(time_t), 1, fp) != 1) {
        lprintf(error, "Failed to read the header of %s!\n", path);
        LinkTable_free(linktbl);
        LinkTable_disk_delete(dirn);
        FREE(path);
        return NULL;
    }
    lprintf(debug, "linktbl->index_time: %d\n", linktbl->index_time);

    linktbl->links = CALLOC(linktbl->num, sizeof(Link *));
    for (int i = 0; i < linktbl->num; i++) {
        linktbl->links[i] = CALLOC(1, sizeof(Link));
        /* The return values are safe to ignore here since we check them
           immediately afterwards with feof() and ferror(). */
        ignore_value(fread(linktbl->links[i]->linkname, sizeof(char),
                           MAX_FILENAME_LEN, fp));
        ignore_value(fread(linktbl->links[i]->f_url, sizeof(char),
                           MAX_PATH_LEN, fp));
        ignore_value(fread(&linktbl->links[i]->type, sizeof(LinkType), 1, fp));
        ignore_value(fread(&linktbl->links[i]->content_length,
                           sizeof(size_t), 1, fp));
        ignore_value(fread(&linktbl->links[i]->time, sizeof(long), 1, fp));
        if (feof(fp)) {
            lprintf(error, "Corrupted LinkTable!\n");
            LinkTable_free(linktbl);
            LinkTable_disk_delete(dirn);
            return NULL;
        }
        if (ferror(fp)) {
            lprintf(error, "Encountered ferror!\n");
            LinkTable_free(linktbl);
            LinkTable_disk_delete(dirn);
            return NULL;
        }
    }
    if (fclose(fp)) {
        lprintf(error,
                "cannot close the file pointer, %s\n", strerror(errno));
    }

    FREE(path);
    return linktbl;
}

LinkTable *path_to_LinkTable(const char *path)
{
    Link *link = NULL;
    Link *tmp_link = NULL;
    Link link_cpy = { 0 };
    LinkTable *next_table = NULL;

    if (!strcmp(path, "/")) {
        next_table = ROOT_LINK_TBL;
        link_cpy = *next_table->links[0];
        tmp_link = &link_cpy;
    } else {
        link = path_to_Link(path);
        tmp_link = link;
    }

    if (!next_table) {
        if (CONFIG.mode == NORMAL) {
            next_table = LinkTable_new(tmp_link->f_url);
        } else if (CONFIG.mode == SINGLE) {
            next_table = single_LinkTable_new(tmp_link->f_url);
        } else if (CONFIG.mode == SONIC) {
            if (!CONFIG.sonic_id3) {
                next_table = sonic_LinkTable_new_index(tmp_link->sonic.id);
            } else {
                next_table =
                    sonic_LinkTable_new_id3(tmp_link->sonic.depth,
                                            tmp_link->sonic.id);
            }
        } else {
            lprintf(fatal, "Invalid CONFIG.mode: %d\n", CONFIG.mode);
        }
    }

    if (link) {
        link->next_table = next_table;
    } else {
        ROOT_LINK_TBL = next_table;
    }

    if (CONFIG.invalid_refresh) {
        LinkTable_uninitialised_fill(next_table);
    }

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
            free(ts.data); /* not FREE(); can be NULL on error path! */
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
    size_t end = start + req_size - 1;

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
        if (!strcasestr((header->data), "Accept-Ranges: bytes") &&
                !strcasestr((header->data), "Content-Range: bytes")) {
            fprintf(stderr, "This web server does not support HTTP range \
requests. If you do not believe that is the case, and if you plan to file a \
bug report, please include the following HTTP header information:\n%s\n",
                    header->data);
            exit(EXIT_FAILURE);
        }
    }

    FREE(header->data);

    long http_resp;
    CURLcode ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
    if (ret) {
        lprintf(error, "%s", curl_easy_strerror(ret));
    }
    if ((http_resp != HTTP_OK) &&
            (http_resp != HTTP_PARTIAL_CONTENT) &&
            (http_resp != HTTP_RANGE_NOT_SATISFIABLE)) {
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

    size_t request_end = offset + req_size;
    if (request_end > link->content_length) {
        lprintf(info,
                "requested size larger than remaining size, request_end: \
%lu, content-length: %ld\n", request_end, link->content_length);
        req_size = link->content_length - offset;
    }

    CURL *curl = Link_download_curl_setup(link, req_size, offset, &header, &ts);

    transfer_blocking(curl);

    curl_off_t recv_sz = Link_download_cleanup(curl, &header);

    if (recv_sz != (long int) req_size) {
        lprintf(error, "req_size != recv, req_size: %lu, recv: %ld\n",
                req_size, recv_sz);
    }

    memmove(output_buf, ts.data, recv_sz);
    FREE(ts.data);

    return recv_sz;
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

static void make_link_relative(const char *page_url, char *link_url)
{
    /*
      Some servers make the links to subdirectories absolute (in URI terms:
      path-absolute), but our code expects them to be relative (in URI terms:
      path-noscheme), so change the contents of link_url as needed to
      accommodate that.

      Also, some servers serve their links as `./name`. This is helpful to
      them because it is the only way to express relative references when the
      first new path segment of the target contains an unescaped colon (`:`),
      eg in `./6:1-balun.png`. While stripping the ./ strictly speaking
      reintroduces that ambiguity, it is of little practical concern in this
      implementation, as full URI link targets are filtered by their number of
      slashes anyway. In URI terms, this converts path-noscheme with a leading
      `.` segment into path-noscheme or path-rootless without that segment.
    */

    if (link_url[0] == '.' && link_url[1] == '/') {
        memmove(link_url, link_url + 2, strlen(link_url) - 1);
        return;
    }

    if (link_url[0] != '/') {
        /* Already relative, nothing to do here!

          (Full URIs, eg. `http://example.com/path`, pass through here
          unmodified, but those are classified in different LinkTypes later
          anyway).
         */
        return;
    }

    /* Find the slash after the host name. */
    int slashes_left_to_find = 3;
    while (*page_url) {
        if (*page_url == '/' && ! --slashes_left_to_find)
            break;
        /* N.B. This is here, rather than doing `while (*page_url++)`, because
           when we're done we want the pointer to point at the final slash. */
        page_url++;
    }
    if (slashes_left_to_find) {
        if (slashes_left_to_find == 1 && ! *page_url)
            /* We're at the top level of the web site and the user entered the URL
               without a trailing slash. */
            page_url = "/";
        else
            /* Well, that's odd. Let's return rather than trying to dig ourselves
               deeper into whatever hole we're in. */
            return;
    }
    /* The page URL is no longer the full page_url, it's just the part after
       the host name. */
    /* The link URL should start with the page URL. */
    if (strstr(link_url, page_url) != link_url)
        return;
    int skip_len = strlen(page_url);
    if (page_url[skip_len-1] != '/') {
        if (page_url[skip_len] != '/')
            /* Um, I'm not sure what to do here, so give up. */
            return;
        skip_len++;
    }
    /* Move the part of the link URL after the parent page's pat to
       the beginning of the link URL string, discarding what came
       before it. */
    memmove(link_url, link_url + skip_len, strlen(link_url) - skip_len + 1);
}
