/*
 * HTTPDirFS - HTTP Directory Filesystem
 *
 * Copyright (C) 2020-2026 Fufu Fang <fangfufu2003@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the OpenSSL
 * library.
 */

/**
 * \file transfer.c
 * \brief Data transfer, curl handles, and download routines implementation
 */

#include "transfer.h"

#include "cache.h"
#include "config.h"
#include "link.h"
#include "link_parser.h"
#include "log.h"
#include "network.h"
#include "url.h"
#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Maximum number of redirect hops we are willing to follow when resolving a
 * link. Redirects are followed manually (FOLLOWLOCATION is off) so each hop can
 * be re-checked against the mounted origin before headers / credentials are
 * sent to it.
 */
#define MAX_REDIRECTS 5

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
        if (ts->ad_ptr) {
            PTHREAD_COND_BROADCAST(&ts->ad_ptr->cond);
        }
        PTHREAD_MUTEX_UNLOCK(&ts->cache_ptr->dl_lock);
    }

    return recv_size;
}

/*
 * Body callback for capped full downloads: like write_memory_callback but
 * stops the transfer as soon as the accumulated size exceeds ts->size_cap.
 * Returning 0 makes libcurl fail with CURLE_WRITE_ERROR, capping the buffer
 * at roughly size_cap instead of buffering an arbitrarily large body.
 */
size_t write_memory_capped_callback(void *recv_data, size_t size, size_t nmemb,
                                    void *userp)
{
    TransferStruct *ts = (TransferStruct *)userp;
    size_t recv_size = size * nmemb;
    if (ts->size_cap > 0 && ts->curr_size + recv_size > ts->size_cap) {
        ts->transferring = 0;
        ts->cap_hit = 1;
        return 0;
    }
    return write_memory_callback(recv_data, size, nmemb, userp);
}

/*
 * Determine the origin reference URL (the mounted server's root) used to decide
 * whether a link is same-origin. Prefer the published root table's head link;
 * fall back to the link's own parent table head link, which is available during
 * table construction, before the root table is published. Returns NULL if no
 * valid reference URL is available.
 */
static const char *origin_base_url(Link *link)
{
    if (ROOT_LINK_TBL && ROOT_LINK_TBL->links && ROOT_LINK_TBL->links[0]) {
        return ROOT_LINK_TBL->links[0]->f_url;
    }
    if (link && link->parent_table && link->parent_table->links
        && link->parent_table->links[0]) {
        return link->parent_table->links[0]->f_url;
    }
    return NULL;
}

/*
 * Decide whether the mounted-server custom headers / credentials should be sent
 * to target_url, given the origin reference base_url. When external origins are
 * disabled everything is same-origin by construction. When they are enabled, a
 * missing base_url fails closed (treated as cross-origin) so credentials and
 * custom headers are never leaked to an unknown origin.
 */
static int is_same_origin_url(const char *base_url, const char *target_url)
{
    if (!CONFIG.allow_external_origin) {
        return 1;
    }
    if (!base_url || !target_url) {
        return 0;
    }
    return !is_cross_origin(base_url, target_url);
}

static int is_same_origin(Link *link)
{
    if (!link) {
        return 0;
    }
    return is_same_origin_url(origin_base_url(link), link->f_url);
}

/*
 * Apply (or clear) the mounted-server custom headers and basic-auth
 * credentials on a curl handle, based on whether target_url is same-origin
 * relative to base_url. This is called once per redirect hop so that the
 * configured headers / credentials are never sent to a cross-origin redirect
 * target.
 */
static void apply_origin_headers(CURL *curl, const char *base_url,
                                 const char *target_url)
{
    int same_origin = is_same_origin_url(base_url, target_url);

    if (CONFIG.http_headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER,
                         same_origin ? (void *)CONFIG.http_headers : NULL);
    }
    if (CONFIG.http_username || CONFIG.http_password) {
        curl_easy_setopt(curl, CURLOPT_USERNAME,
                         same_origin ? (void *)CONFIG.http_username : NULL);
        curl_easy_setopt(curl, CURLOPT_PASSWORD,
                         same_origin ? (void *)CONFIG.http_password : NULL);
    }
}

CURL *Link_to_curl(Link *link)
{
    CURL *curl = curl_easy_init();
    if (!curl) {
        lprintf(fatal, "curl_easy_init() failed!\n");
    }
    /*
     * set up some basic curl stuff
     */
    CURLcode ret = curl_easy_setopt(curl, CURLOPT_USERAGENT, CONFIG.user_agent);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    /*
     * Disable automatic redirect following. Redirects are handled one hop at a
     * time by the download loops (follow_one_redirect) so that each target is
     * re-checked against the mounted origin before the custom headers and
     * credentials are sent to it. This also handles following directories that
     * lack the trailing '/'.
     */
    ret = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    /*
     * Safety cap in case automatic following is ever re-enabled; the manual
     * loop enforces MAX_REDIRECTS hops itself.
     */
    ret = curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 2);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_URL, link->f_url);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_PIPEWAIT, 1L);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    if (curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_3)) {
        ret = curl_easy_setopt(curl, CURLOPT_HTTP_VERSION,
                               CURL_HTTP_VERSION_2_0);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    }
    ret = curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_SHARE, CURL_SHARE);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    if (CONFIG.cafile || CONFIG.capath) {
        /*
         * Having been given a certificate file or directory, disable any search
         * paths built into libcurl, so that we exclusively use the explicitly
         * given certificate(s).
         */
        ret = curl_easy_setopt(curl, CURLOPT_CAPATH, CONFIG.capath);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }

        ret = curl_easy_setopt(curl, CURLOPT_CAINFO, CONFIG.cafile);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.insecure_tls) {
        ret = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.log_type & libcurl_debug) {
        ret = curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    }

    /*
     * Apply the mounted-server custom headers and basic-auth credentials only
     * when the link is same-origin. When --allow-external-origin is active,
     * cross-origin links must NOT receive the user's credentials or custom
     * headers for the primary server. Redirect hops are re-checked per hop by
     * the download loops, so a cross-origin redirect target never receives
     * them either.
     */
    apply_origin_headers(curl, origin_base_url(link), link->f_url);

    if (CONFIG.proxy) {
        ret = curl_easy_setopt(curl, CURLOPT_PROXY, CONFIG.proxy);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.proxy_username) {
        ret = curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME,
                               CONFIG.proxy_username);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.proxy_password) {
        ret = curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD,
                               CONFIG.proxy_password);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    }

    if (CONFIG.proxy_cafile || CONFIG.proxy_capath) {
        /* See CONFIG.cafile above */
        ret = curl_easy_setopt(curl, CURLOPT_PROXY_CAPATH, CONFIG.proxy_capath);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }

        ret = curl_easy_setopt(curl, CURLOPT_PROXY_CAINFO, CONFIG.proxy_cafile);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    }

    return curl;
}

/*
 * If the just-completed (single, non-redirect-following) transfer was an HTTP
 * redirect, re-point the handle at the resolved target and re-apply the
 * same-origin header / credential decision for that target.
 *
 * Automatic redirect following is disabled on the handle (see Link_to_curl), so
 * this walks a redirect chain one hop at a time. It returns 1 if a redirect was
 * followed (the caller must run the transfer again for the new target), 0
 * otherwise. The caller's loop resets the body / header buffers before the next
 * transfer. CONFIG.http_headers / credentials are only (re)sent to a
 * same-origin target, so they are never leaked to a cross-origin redirect.
 */
static int follow_one_redirect(CURL *curl, const char *base_url)
{
    long http_resp = 0;
    if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp)) {
        return 0;
    }
    if (http_resp < 300 || http_resp >= 400) {
        return 0;
    }

    char *redir_url = NULL;
    if (curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &redir_url) || !redir_url
        || !redir_url[0]) {
        return 0;
    }

    CURLcode ret = curl_easy_setopt(curl, CURLOPT_URL, redir_url);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
        return 0;
    }

    apply_origin_headers(curl, base_url, redir_url);
    return 1;
}

static void filestat_on_complete(TransferStruct *ts, CURL *curl,
                                 CURLcode result, const char *url)
{
    if (!result) {
        /*
         * Follow redirects one hop at a time (FOLLOWLOCATION is off) so each
         * hop re-checks the target against the mounted origin before custom
         * headers / credentials are sent.
         */
        if (follow_one_redirect(curl, origin_base_url(ts->link))) {
            ts->redirects++;
            if (ts->redirects > MAX_REDIRECTS) {
                lprintf(error, "too many redirects for %s\n", ts->link->f_url);
                ts->link->type = LINK_INVALID;
                curl_easy_cleanup(curl);
                FREE(ts->data);
                FREE(ts);
                return;
            }
            /*
             * Reset the accumulated header buffer so only the final hop's
             * headers are cached, then re-enqueue. on_complete runs with
             * transfer_lock already held, so use the lock-free requeue.
             */
            FREE(ts->data);
            ts->curr_size = 0;
            ts->transferring = 1;
            transfer_requeue_locked(curl);
            return;
        }

        /*
         * Transfer successful, set the file size
         */
        Link_set_file_stat(ts->link, curl);
        if (CACHE_SYSTEM_INIT) {
            char *eff_url = NULL;
            long http_resp = 0;
            curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff_url);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
            char *ct = NULL;
            curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);

            int bypass = (http_resp != HTTP_OK)
                         || (ts->link->type != LINK_FILE
                             && ts->link->type != LINK_DIR);
            if (ts->link->type == LINK_FILE) {
                off_t file_size = (off_t)ts->link->content_length;
                if ((CONFIG.cache_min_size >= 0
                     && file_size < CONFIG.cache_min_size)
                    || (CONFIG.cache_max_size >= 0
                        && file_size > CONFIG.cache_max_size)) {
                    bypass = 1;
                }
            }

            if (!bypass) {
                const char *target_url
                    = (eff_url && eff_url[0]) ? eff_url : ts->link->f_url;
                CacheContainer_write_head(
                    target_url, http_resp, (curl_off_t)ts->link->content_length,
                    ts->link->time, ct ? ct : "", ts->data, ts->curr_size,
                    ts->link->type);

                if (eff_url && eff_url[0]
                    && strcmp(eff_url, ts->link->f_url) != 0) {
                    CacheContainer_write_redirect(ts->link->f_url, eff_url,
                                                  http_resp);
                }
            }
        }
    } else {
        lprintf(error, "%d - %s <%s>\n", result, curl_easy_strerror(result),
                url ? url : "");
        /*
         * If the transfer failed, and we are querying the file size,
         * we must mark the link as invalid so that the link table
         * fill function can proceed.
         */
        ts->link->type = LINK_INVALID;
    }
    curl_easy_cleanup(curl);
    FREE(ts->data);
    FREE(ts);
}

void Link_req_file_stat(Link *this_link)
{
    CURL *curl = Link_to_curl(this_link);
    CURLcode ret = curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_FILETIME, 1L);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
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
    transfer->transferring = 1;
    transfer->on_complete = filestat_on_complete;
    ret = curl_easy_setopt(curl, CURLOPT_PRIVATE, transfer);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_memory_callback);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)transfer);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }

    transfer_nonblocking(curl);
}

LinkType Link_classify_response(LinkType current_type, long http_resp,
                                curl_off_t cl, const char *content_type,
                                size_t *content_len_out)
{
    if (http_resp != HTTP_OK) {
        return LINK_INVALID;
    }

    if (current_type == LINK_UNINITIALISED_DIR) {
        return LINK_DIR;
    }

    if (current_type != LINK_UNINITIALISED_FILE) {
        return current_type;
    }

    if (CONFIG.html_is_directory && is_html_content_type(content_type)) {
        if (cl > 0 && (off_t)cl > CONFIG.max_html_size) {
            if (content_len_out) {
                *content_len_out = (size_t)cl;
            }
            return LINK_FILE;
        }
        return LINK_DIR;
    }

    if (cl < 0) {
        /*
         * Unknown size (chunked / no Content-Length): we cannot report a size
         * without downloading the whole body, so decide purely on content
         * type (both flag modes).
         *   - A concrete non-HTML type (e.g. image/png, application/zip) is
         *     trusted to be a plain file, not a listing, so parsing it is
         *     pointless; and its size is unknown, so it cannot be presented
         *     as a file either. Hide it.
         *   - HTML, or no/empty content type, is a tentative directory that
         *     may degrade to an empty folder on first browse. A missing
         *     content type is common for on-the-fly generated directory
         *     listings, which also lack a Content-Length.
         */
        int is_html = is_html_content_type(content_type);
        int has_ct = content_type != NULL && *content_type != '\0';
        if (has_ct && !is_html) {
            return LINK_INVALID;
        }
        return LINK_DIR;
    }
    if (cl == 0 && CONFIG.zero_len_is_dir) {
        return LINK_DIR;
    }
    if (content_len_out) {
        *content_len_out = (size_t)cl;
    }
    return LINK_FILE;
}

void Link_set_file_stat(Link *this_link, CURL *curl)
{
    long http_resp;
    CURLcode ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    if (http_resp == HTTP_OK) {
        curl_off_t cl = 0;
        ret = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
        ret = curl_easy_getinfo(curl, CURLINFO_FILETIME, &(this_link->time));
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }

        char *content_type = NULL;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type);

        size_t content_len = 0;
        this_link->type = Link_classify_response(this_link->type, http_resp, cl,
                                                 content_type, &content_len);
        if (this_link->type == LINK_FILE) {
            this_link->content_length = content_len;
        }

    } else {
        lprintf(warning, "%s: HTTP %ld\n", this_link->f_url, http_resp);
        /*
         * Emit a targeted warning if an external link needs authentication
         * that we are not providing.
         */
        if (CONFIG.allow_external_origin
            && (http_resp == 401 || http_resp == 403)
            && !is_same_origin(this_link)) {
            lprintf(warning,
                    "External link %s requires authentication (HTTP %ld). "
                    "Credentials are only applied to the mounted "
                    "server.\n",
                    this_link->f_url, http_resp);
        }
        if (HTTP_temp_failure((HTTPResponseCode)http_resp)
            || CONFIG.invalid_refresh) {
            lprintf(warning, ", retrying later.\n");
        } else {
            this_link->type = LINK_INVALID;
        }
    }
}

TransferStruct Link_download_full(Link *link, TransferStruct *header_out)
{
    char *url = link->f_url;
    CURL *curl = Link_to_curl(link);

    TransferStruct ts = {0};
    ts.type = DATA;
    ts.transferring = 1;

    /*
     * When --html-is-directory is active, cap the full-body download at
     * max_html_size so an oversized *promoted* directory (a no-trailing-slash
     * HTML page that we tentatively exposed as a directory) does not buffer
     * its entire body in memory. The callback aborts the transfer once the
     * cap is exceeded (CURLE_WRITE_ERROR) and the caller treats the result as
     * an empty folder.
     *
     * Real directories (path ends with '/') are exempt: their listing is
     * unambiguously a directory listing and may legitimately exceed
     * max_html_size, so it is downloaded in full.
     */
    int capped = 0;
    if (CONFIG.html_is_directory && CONFIG.max_html_size > 0) {
        const char *qf = strpbrk(url, "?#");
        size_t tlen = qf ? (size_t)(qf - url) : strlen(url);
        int real_dir = (tlen > 0 && url[tlen - 1] == '/');
        if (!real_dir) {
            ts.size_cap = (size_t)CONFIG.max_html_size;
            capped = 1;
        }
    }

    TransferStruct header_local = {0};
    TransferStruct *header_ptr = header_out ? header_out : &header_local;
    header_ptr->curr_size = 0;
    header_ptr->data = NULL;
    header_ptr->type = DATA;

    CURLcode ret = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                                    capped ? write_memory_capped_callback
                                           : write_memory_callback);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&ts);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_PRIVATE, (void *)&ts);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)header_ptr);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }

    /*
     * If we get temporary HTTP failure, wait for 5 seconds before retry
     */
    const char *base_url = origin_base_url(link);
    int redirects = 0;
    long http_resp = 0;
    do {
        /*
         * Reset the transfer struct for each attempt to avoid accumulating
         * data from failed/partial attempts.
         */
        FREE(ts.data);
        ts.curr_size = 0;
        ts.transferring = 1;
        ts.cap_hit = 0;

        FREE(header_ptr->data);
        header_ptr->curr_size = 0;

        transfer_blocking(curl);
        ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
        /*
         * Redirects are followed one hop at a time (FOLLOWLOCATION is off).
         * Each hop re-checks the target against the mounted origin before the
         * custom headers / credentials are sent, so a cross-origin redirect
         * never receives them.
         */
        if (follow_one_redirect(curl, base_url)) {
            redirects++;
            if (redirects > MAX_REDIRECTS) {
                lprintf(error, "too many redirects for %s\n", url);
                ts.failed = 1;
                ts.curr_size = 0;
                free(ts.data);
                ts.data = NULL;
                if (!header_out) {
                    free(header_ptr->data);
                    header_ptr->data = NULL;
                }
                curl_easy_cleanup(curl);
                return ts;
            }
            continue;
        }
        if (HTTP_temp_failure((HTTPResponseCode)http_resp)) {
            lprintf(warning, "URL: %s, HTTP %ld, retrying later.\n", url,
                    http_resp);
            sleep(CONFIG.http_wait_sec);
        } else if (http_resp != HTTP_OK) {
            lprintf(warning, "cannot retrieve URL: %s, HTTP %ld\n", url,
                    http_resp);
            ts.failed = 1;
            ts.curr_size = 0;
            free(ts.data); /* not FREE(); can be NULL on error path! */
            ts.data = NULL;
            if (!header_out) {
                free(header_ptr->data);
                header_ptr->data = NULL;
            }
            curl_easy_cleanup(curl);
            return ts;
        }
    } while (HTTP_temp_failure((HTTPResponseCode)http_resp));

    ret = curl_easy_getinfo(curl, CURLINFO_FILETIME, &(link->time));
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    char *eff_url = NULL;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff_url);
    if (CACHE_SYSTEM_INIT && eff_url && eff_url[0]
        && strcmp(eff_url, url) != 0) {
        CacheContainer_write_redirect(url, eff_url, http_resp);
    }
    if (eff_url && eff_url[0]) {
        ts.eff_url = STRDUP(eff_url);
    }
    if (!header_out) {
        FREE(header_local.data);
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
    CURL *curl = Link_to_curl(link);
    CURLcode ret = curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void *)header);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)ts);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_PRIVATE, (void *)ts);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    ret = curl_easy_setopt(curl, CURLOPT_RANGE, range_str);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }

    return curl;
}

static curl_off_t Link_download_cleanup(CURL *curl, TransferStruct *header)
{
    /*
     * Check for range seek support
     */
    if (header->data && !CONFIG.no_range_check
        && !strcasestr(header->data, "Accept-Ranges: bytes")
        && !strcasestr(header->data, "Content-Range: bytes")) {
        fprintf(stderr,
                "This web server does not support HTTP range requests. "
                "If you do not believe that is the case, and if you plan "
                "to file a bug report, please include the following HTTP "
                "header information:\n%s\n",
                header->data);
        exit(EXIT_FAILURE);
    }

    FREE(header->data);

    long http_resp;
    CURLcode ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_resp);
    if (ret) {
        lprintf(error, "%s\n", curl_easy_strerror(ret));
    }
    curl_off_t recv = -1;
    if ((http_resp == HTTP_OK) || (http_resp == HTTP_PARTIAL_CONTENT)
        || (http_resp == HTTP_RANGE_NOT_SATISFIABLE)) {
        ret = curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &recv);
        if (ret) {
            lprintf(error, "%s\n", curl_easy_strerror(ret));
        }
    } else {
        char *url;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);
        lprintf(warning, "Could not download %s, HTTP %ld\n", url, http_resp);
        if (HTTP_temp_failure((HTTPResponseCode)http_resp)) {
            recv = -EAGAIN;
        } else {
            recv = -ENOENT;
        }
    }

    curl_easy_cleanup(curl);

    return recv;
}

static void Link_download_finish_transfer(Cache *cf, off_t offset,
                                          TransferStruct *ts)
{
    if (!cf) {
        ts->transferring = 0;
        return;
    }

    PTHREAD_MUTEX_LOCK(&cf->dl_lock);
    ts->transferring = 0;
    ActiveDownload *ad = ActiveDownload_find(cf, offset);
    if (ad && ad->ts == ts) {
        ad->ts = NULL;
    }
    if (ts->ad_ptr) {
        ActiveDownload_unref(ts->ad_ptr);
        ts->ad_ptr = NULL;
    }
    PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
}

long Link_download(Link *link, char *output_buf, size_t req_size, off_t offset,
                   Cache *cf)
{
    if (req_size == 0 || link->content_length == 0 || offset < 0
        || (size_t)offset >= link->content_length) {
        return 0;
    }

    TransferStruct ts = {0};
    TransferStruct header = {0};
    curl_off_t recv_sz;
    const char *base_url = origin_base_url(link);

    size_t remaining = link->content_length - (size_t)offset;
    if (req_size > remaining) {
        lprintf(debug, "requested size larger than remaining size, req_size: \
%zu, remaining: %zu\n",
                req_size, remaining);
        req_size = remaining;
    }

    do {
        ts.curr_size = 0;
        ts.data = NULL;
        ts.type = DATA;
        ts.transferring = 1;
        ts.cache_ptr = cf;
        ts.ad_ptr = NULL;

        if (cf) {
            PTHREAD_MUTEX_LOCK(&cf->dl_lock);
            ActiveDownload *ad = ActiveDownload_find(cf, offset);
            if (ad) {
                ad->ts = &ts;
                ts.ad_ptr = ad;
                ad->refcount++;
                PTHREAD_COND_BROADCAST(&ad->cond);
            }
            PTHREAD_MUTEX_UNLOCK(&cf->dl_lock);
        }

        header.curr_size = 0;
        header.data = NULL;
        header.cache_ptr = NULL;

        CURL *curl
            = Link_download_curl_setup(link, req_size, offset, &header, &ts);

        /*
         * Follow redirects one hop at a time on the same handle (FOLLOWLOCATION
         * is off). The Range header is preserved across hops so the same byte
         * range is requested from the redirect target; each hop re-checks the
         * target against the mounted origin before the custom headers /
         * credentials are sent to it.
         */
        int redirects = 0;
        for (;;) {
            FREE(ts.data);
            ts.curr_size = 0;
            ts.transferring = 1;
            FREE(header.data);
            header.curr_size = 0;

            transfer_blocking(curl);

            if (!follow_one_redirect(curl, base_url)) {
                break;
            }
            if (++redirects > MAX_REDIRECTS) {
                lprintf(error, "too many redirects for %s\n", link->f_url);
                break;
            }
        }

        recv_sz = Link_download_cleanup(curl, &header);

        if (recv_sz < 0) {
            Link_download_finish_transfer(cf, offset, &ts);
            FREE(ts.data);
            if (recv_sz == -EAGAIN) {
                lprintf(warning, "HTTP temporary failure, retrying...\n");
                sleep(CONFIG.http_wait_sec);
                continue;
            }
            return recv_sz;
        }

        if (recv_sz != (long int)req_size) {
            /*
             * A successful response that is shorter than the requested range
             * is a data-integrity failure: the bytes we have cannot be trusted
             * to be contiguous. Surface EIO instead of retrying, which would
             * otherwise loop (and sleep) indefinitely.
             */
            lprintf(error, "req_size != recv, req_size: %lu, recv: %ld\n",
                    req_size, recv_sz);
            Link_download_finish_transfer(cf, offset, &ts);
            FREE(ts.data);
            return -EIO;
        }

        /* success */
        break;
    } while (1);

    Link_download_finish_transfer(cf, offset, &ts);

    /*
     * Reached only on success, where recv_sz == req_size > 0, so ts.data is
     * non-NULL; guard anyway for the degenerate zero-byte case.
     */
    if (ts.data) {
        memmove(output_buf, ts.data, recv_sz);
    }
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

    long res;
    if (link->is_virtual) {
        if (offset < 0 || (size_t)offset >= link->content_length
            || !link->virtual_content || req_size == 0) {
            res = 0;
        } else {
            size_t remaining = link->content_length - (size_t)offset;
            if (req_size > remaining) {
                req_size = remaining;
            }
            memcpy(output_buf, link->virtual_content + offset, req_size);
            res = (long)req_size;
        }
    } else {
        res = Link_download(link, output_buf, req_size, offset, NULL);
    }
    LinkTable_unref(link->parent_table);
    return res;
}
