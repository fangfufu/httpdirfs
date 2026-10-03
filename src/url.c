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
 * \file url.c
 * \brief URL parsing, origin matching, and URI manipulation implementation
 */

#include "url.h"

#include "config.h"
#include "link.h"
#include "log.h"
#include "util.h"

#include <ctype.h>
#include <curl/curl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>

int is_external_url(const char *url)
{
    if (!url) {
        return 0;
    }
    return (strncasecmp(url, "http://", 7) == 0
            || strncasecmp(url, "https://", 8) == 0);
}

static int parse_origin(const char *url, size_t origin_len, char *scheme,
                        size_t scheme_max, char *host, size_t host_max,
                        int *port)
{
    if (!url || origin_len == 0) {
        return -1;
    }

    // Find "://" within origin_len
    const char *scheme_end = NULL;
    for (size_t i = 0; i + 2 < origin_len; i++) {
        if (url[i] == ':' && url[i + 1] == '/' && url[i + 2] == '/') {
            scheme_end = url + i;
            break;
        }
    }

    if (!scheme_end) {
        return -1; // malformed origin
    }

    size_t scheme_len = (size_t)(scheme_end - url);
    if (scheme_len >= scheme_max) {
        return -1;
    }
    strncpy(scheme, url, scheme_len);
    scheme[scheme_len] = '\0';
    // Lowercase scheme
    for (size_t i = 0; i < scheme_len; i++) {
        if (scheme[i] >= 'A' && scheme[i] <= 'Z') {
            scheme[i] += 32;
        }
    }

    const char *host_start = scheme_end + 3;
    const char *origin_end = url + origin_len;
    if (host_start >= origin_end) {
        return -1;
    }

    size_t host_part_len = (size_t)(origin_end - host_start);

    // Check for IPv6 bracketed host
    const char *bracket_close = NULL;
    if (*host_start == '[') {
        for (const char *p = host_start; p < origin_end; p++) {
            if (*p == ']') {
                bracket_close = p;
                break;
            }
        }
    }

    const char *port_colon = NULL;
    if (bracket_close) {
        // Look for colon after the closing bracket
        for (const char *p = bracket_close + 1; p < origin_end; p++) {
            if (*p == ':') {
                port_colon = p;
                break;
            }
        }
    } else {
        // Look for colon in the host part (there should be at most one colon if
        // no brackets)
        for (const char *p = host_start; p < origin_end; p++) {
            if (*p == ':') {
                port_colon = p;
                break;
            }
        }
    }

    size_t host_len;
    if (port_colon) {
        host_len = (size_t)(port_colon - host_start);
        // Parse port
        int p_val = 0;
        for (const char *p = port_colon + 1; p < origin_end; p++) {
            if (*p >= '0' && *p <= '9') {
                p_val = p_val * 10 + (*p - '0');
            } else {
                return -1; // invalid character in port
            }
        }
        *port = p_val;
    } else {
        host_len = host_part_len;
        // Default port based on scheme
        if (strcmp(scheme, "http") == 0) {
            *port = 80;
        } else if (strcmp(scheme, "https") == 0) {
            *port = 443;
        } else {
            *port = 0; // unknown scheme default port
        }
    }

    if (host_len >= host_max) {
        return -1;
    }
    strncpy(host, host_start, host_len);
    host[host_len] = '\0';
    // Lowercase host
    for (size_t i = 0; i < host_len; i++) {
        if (host[i] >= 'A' && host[i] <= 'Z') {
            host[i] += 32;
        }
    }

    return 0;
}

char *get_server_root(const char *url)
{
    if (!url) {
        return NULL;
    }

    int slashes = 0;
    const char *p = url;
    while (*p && slashes < 3 && *p != '?' && *p != '#') {
        if (*p == '/') {
            slashes++;
        }
        p++;
    }

    size_t origin_len;
    if (slashes < 3) {
        if (is_external_url(url)) {
            const char *q = strpbrk(url, "?#");
            origin_len = q ? (size_t)(q - url) : strlen(url);
        } else {
            return NULL;
        }
    } else {
        origin_len = (size_t)(p - url) - 1;
    }

    char scheme[32];
    char host[PATH_MAX];
    int port = 0;
    if (parse_origin(url, origin_len, scheme, sizeof(scheme), host,
                     sizeof(host), &port)
        != 0) {
        return NULL;
    }

    int is_default_port = ((strcmp(scheme, "http") == 0 && port == 80)
                           || (strcmp(scheme, "https") == 0 && port == 443));

    char buf[PATH_MAX + 64];
    if (port > 0 && !is_default_port) {
        snprintf(buf, sizeof(buf), "%s://%s:%d", scheme, host, port);
    } else {
        snprintf(buf, sizeof(buf), "%s://%s", scheme, host);
    }

    return STRDUP(buf);
}

int is_cross_origin(const char *page_url, const char *link_url)
{
    if (!page_url || !link_url) {
        return 1;
    }
    char *root1 = get_server_root(page_url);
    char *root2 = get_server_root(link_url);
    if (!root1 || !root2) {
        if (root1) {
            FREE(root1);
        }
        if (root2) {
            FREE(root2);
        }
        return 1;
    }
    int res = (strcasecmp(root1, root2) != 0);
    FREE(root1);
    FREE(root2);
    return res;
}

const char *get_url_path_from_server_root(const char *url)
{
    if (!url) {
        return NULL;
    }
    const char *scheme_sep = strstr(url, "://");
    if (!scheme_sep) {
        return url;
    }
    const char *host_start = scheme_sep + 3;
    const char *p = host_start;
    while (*p && *p != '/' && *p != '?' && *p != '#') {
        p++;
    }
    return p;
}


static void normalize_url_path(char *url)
{
    if (!url) {
        return;
    }
    char *scheme_sep = strstr(url, "://");
    if (!scheme_sep) {
        return;
    }
    char *path_start = strchr(scheme_sep + 3, '/');
    if (!path_start) {
        return;
    }

    /* Fast check if any dot segments exist */
    if (!strstr(path_start, "/.") && strcmp(path_start, "/.") != 0
        && strcmp(path_start, "/..") != 0) {
        return;
    }

    char *qf = strpbrk(path_start, "?#");
    char saved_qf_char = '\0';
    if (qf) {
        saved_qf_char = *qf;
        *qf = '\0';
    }

    size_t orig_len = strlen(path_start);
    int trailing_slash = (orig_len > 0 && path_start[orig_len - 1] == '/');

    const char *seg_start[256];
    size_t seg_len[256];
    int nsegs = 0;

    const char *p = path_start;
    while (*p) {
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        const char *next = strchr(p, '/');
        size_t len = next ? (size_t)(next - p) : strlen(p);

        if (len == 1 && p[0] == '.') {
            if (!next) {
                trailing_slash = 1;
            }
        } else if (len == 2 && p[0] == '.' && p[1] == '.') {
            if (nsegs > 0) {
                nsegs--;
            }
            if (!next) {
                trailing_slash = 1;
            }
        } else {
            if (nsegs < 256) {
                seg_start[nsegs] = p;
                seg_len[nsegs] = len;
                nsegs++;
            }
        }

        if (next) {
            p = next;
        } else {
            break;
        }
    }

    char *dest = path_start;
    *dest++ = '/';
    for (int i = 0; i < nsegs; i++) {
        memmove(dest, seg_start[i], seg_len[i]);
        dest += seg_len[i];
        if (i < nsegs - 1 || trailing_slash) {
            *dest++ = '/';
        }
    }
    *dest = '\0';

    if (qf) {
        *qf = saved_qf_char;
        size_t qf_len = strlen(qf);
        memmove(dest, qf, qf_len + 1);
    }
}

int resolve_target_url(const char *page_url, const char *raw_href,
                       char *out_url, size_t out_size)
{
    if (!page_url || !raw_href || !out_url || out_size == 0) {
        return 0;
    }

    /* Trim leading and trailing whitespace from raw_href */
    while (*raw_href && isspace((unsigned char)*raw_href)) {
        raw_href++;
    }
    if (*raw_href == '\0') {
        return 0;
    }
    size_t href_len = strlen(raw_href);
    while (href_len > 0 && isspace((unsigned char)raw_href[href_len - 1])) {
        href_len--;
    }

    /* Ignore fragment in href */
    const char *hash_pos = memchr(raw_href, '#', href_len);
    if (hash_pos) {
        href_len = (size_t)(hash_pos - raw_href);
    }
    if (href_len == 0) {
        return 0;
    }

    char resolved[PATH_MAX + 1];
    size_t resolved_len = 0;

    /* 1. Absolute URL (http:// or https://) */
    if ((href_len >= 7 && strncasecmp(raw_href, "http://", 7) == 0)
        || (href_len >= 8 && strncasecmp(raw_href, "https://", 8) == 0)) {
        if (href_len >= sizeof(resolved)) {
            return 0;
        }
        memcpy(resolved, raw_href, href_len);
        resolved[href_len] = '\0';
        resolved_len = href_len;
    } else if (href_len >= 2 && raw_href[0] == '/' && raw_href[1] == '/') {
        /* 2. Scheme-relative URL (//...) */
        const char *colon = strchr(page_url, ':');
        if (!colon) {
            return 0;
        }
        size_t scheme_len = (size_t)(colon - page_url) + 1; /* includes ':' */
        if (scheme_len + href_len >= sizeof(resolved)) {
            return 0;
        }
        memcpy(resolved, page_url, scheme_len);
        memcpy(resolved + scheme_len, raw_href, href_len);
        resolved[scheme_len + href_len] = '\0';
        resolved_len = scheme_len + href_len;
    } else if (raw_href[0] == '/') {
        /* 3. Origin-relative URL (/...) */
        const char *scheme_sep = strstr(page_url, "://");
        if (!scheme_sep) {
            return 0;
        }
        const char *origin_end = strchr(scheme_sep + 3, '/');
        size_t origin_len
            = origin_end ? (size_t)(origin_end - page_url) : strlen(page_url);
        if (origin_len + href_len >= sizeof(resolved)) {
            return 0;
        }
        memcpy(resolved, page_url, origin_len);
        memcpy(resolved + origin_len, raw_href, href_len);
        resolved[origin_len + href_len] = '\0';
        resolved_len = origin_len + href_len;
    } else {
        /* 4. Path-relative URL (does not start with '/') */
        size_t page_len = strlen(page_url);
        size_t base_len = 0;
        if (page_url[page_len - 1] == '/') {
            base_len = page_len;
        } else {
            const char *scheme_sep = strstr(page_url, "://");
            const char *last_slash = strrchr(page_url, '/');
            if (scheme_sep && last_slash && last_slash > scheme_sep + 2) {
                base_len = (size_t)(last_slash - page_url) + 1;
            } else {
                base_len = page_len;
            }
        }

        int need_slash = (page_url[base_len - 1] != '/');
        if (base_len + (need_slash ? 1 : 0) + href_len >= sizeof(resolved)) {
            return 0;
        }

        memcpy(resolved, page_url, base_len);
        if (need_slash) {
            resolved[base_len] = '/';
            base_len++;
        }
        memcpy(resolved + base_len, raw_href, href_len);
        resolved[base_len + href_len] = '\0';
        resolved_len = base_len + href_len;
    }

    /* Encode spaces as %20 so that libcurl and web servers can parse the URL */
    size_t out_pos = 0;
    for (size_t i = 0; i < resolved_len; i++) {
        if (resolved[i] == ' ') {
            if (out_pos + 3 >= out_size) {
                return 0;
            }
            out_url[out_pos++] = '%';
            out_url[out_pos++] = '2';
            out_url[out_pos++] = '0';
        } else {
            if (out_pos + 1 >= out_size) {
                return 0;
            }
            out_url[out_pos++] = resolved[i];
        }
    }
    out_url[out_pos] = '\0';
    normalize_url_path(out_url);
    return 1;
}

int extract_url_path_segments(const char *url, char ***segments_out,
                              int *num_segments_out)
{
    if (!segments_out || !num_segments_out) {
        return 0;
    }
    *segments_out = NULL;
    *num_segments_out = 0;

    if (!url || *url == '\0') {
        return 0;
    }

    /* 1. Strip query parameters and fragment */
    const char *qf = strpbrk(url, "?#");
    size_t url_len = qf ? (size_t)(qf - url) : strlen(url);

    /* 2. Skip scheme and host to isolate path */
    const char *path = url;
    const char *scheme_sep = strstr(url, "://");
    if (scheme_sep && (size_t)(scheme_sep - url) < url_len) {
        const char *after_host = strchr(scheme_sep + 3, '/');
        if (after_host && (!qf || after_host < qf)) {
            path = after_host;
        } else {
            return 0;
        }
    } else if (url_len >= 2 && url[0] == '/' && url[1] == '/') {
        const char *after_host = strchr(url + 2, '/');
        if (after_host && (!qf || after_host < qf)) {
            path = after_host;
        } else {
            return 0;
        }
    }

    size_t path_len = qf ? (size_t)(qf - path) : strlen(path);

    /* 3. Strip trailing slashes */
    while (path_len > 0 && path[path_len - 1] == '/') {
        path_len--;
    }
    if (path_len == 0) {
        return 0;
    }

    /* 4. Count non-empty segments */
    int count = 0;
    size_t i = 0;
    while (i < path_len) {
        while (i < path_len && path[i] == '/') {
            i++;
        }
        if (i < path_len) {
            count++;
            while (i < path_len && path[i] != '/') {
                i++;
            }
        }
    }

    if (count == 0) {
        return 0;
    }

    char **segs = (char **)CALLOC((size_t)count, sizeof(char *));
    int idx = 0;
    i = 0;
    while (i < path_len && idx < count) {
        while (i < path_len && path[i] == '/') {
            i++;
        }
        if (i < path_len) {
            size_t start = i;
            while (i < path_len && path[i] != '/') {
                i++;
            }
            size_t seg_len = i - start;

            int unescaped_len = 0;
            char *unescaped = curl_easy_unescape(NULL, path + start,
                                                 (int)seg_len, &unescaped_len);
            char *seg_str = NULL;
            if (unescaped) {
                seg_str = STRNDUP(unescaped, (size_t)unescaped_len);
                curl_free(unescaped);
            } else {
                seg_str = STRNDUP(path + start, seg_len);
            }

            for (char *c = seg_str; *c; c++) {
                if (*c == '/') {
                    *c = '_';
                }
            }
            segs[idx++] = seg_str;
        }
    }

    *segments_out = segs;
    *num_segments_out = idx;
    return idx;
}

void free_url_path_segments(char **segments, int num_segments)
{
    if (!segments) {
        return;
    }
    for (int i = 0; i < num_segments; i++) {
        FREE(segments[i]);
    }
    FREE(segments);
}

int url_matches_head_link(const char *target_url, const char *head_url)
{
    if (!target_url || !head_url) {
        return 0;
    }

    char *unescaped_target = curl_easy_unescape(NULL, target_url, 0, NULL);
    char *unescaped_head = curl_easy_unescape(NULL, head_url, 0, NULL);
    const char *tgt = unescaped_target ? unescaped_target : target_url;
    const char *head = unescaped_head ? unescaped_head : head_url;

    const char *tgt_end = tgt + strlen(tgt);
    const char *head_end = head + strlen(head);

    /* If head_url has no query string, ignore query string on target_url */
    if (!strchr(head, '?')) {
        const char *q = strchr(tgt, '?');
        if (q) {
            tgt_end = q;
        }
    }

    /* Strip trailing slashes, but keep root slash after scheme://host */
    const char *tgt_slash = strstr(tgt, "://");
    const char *tgt_min = tgt_slash ? tgt_slash + 3 : tgt;
    const char *head_slash = strstr(head, "://");
    const char *head_min = head_slash ? head_slash + 3 : head;

    while (tgt_end > tgt_min && *(tgt_end - 1) == '/') {
        tgt_end--;
    }
    while (head_end > head_min && *(head_end - 1) == '/') {
        head_end--;
    }

    size_t tgt_len = (size_t)(tgt_end - tgt);
    size_t head_len = (size_t)(head_end - head);

    int match = (tgt_len == head_len && strncmp(tgt, head, tgt_len) == 0);

    if (unescaped_target) {
        curl_free(unescaped_target);
    }
    if (unescaped_head) {
        curl_free(unescaped_head);
    }
    return match;
}

char *string_to_cache_path(const char *source)
{
    if (!source || !source[0]) {
        return NULL;
    }

    /*
     * 1-level hash sharding: "<origin>/<first 2 hex of md5>/<full md5>".
     * The shard directory completely eliminates file-versus-directory path
     * collisions, and the 32-character hex filename is immune to the Linux
     * NAME_MAX / PATH_MAX limits.
     */
    char *hash = generate_md5sum(source);
    if (!hash) {
        return NULL;
    }
    char rel_path[64];
    snprintf(rel_path, sizeof(rel_path), "%.2s/%s", hash, hash);
    char *result = STRDUP(rel_path);
    FREE(hash);
    return result;
}

static int is_unreserved_char(unsigned char c)
{
    return (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~');
}

static void normalize_percent_encoding(char *str)
{
    if (!str) {
        return;
    }
    char *src = str;
    char *dst = str;
    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)src[1])
            && isxdigit((unsigned char)src[2])) {
            int h1 = tolower((unsigned char)src[1]);
            int h2 = tolower((unsigned char)src[2]);
            int v1 = (h1 >= 'a') ? (h1 - 'a' + 10) : (h1 - '0');
            int v2 = (h2 >= 'a') ? (h2 - 'a' + 10) : (h2 - '0');
            unsigned char byte = (unsigned char)((v1 << 4) | v2);
            if (is_unreserved_char(byte)) {
                *dst++ = (char)byte;
            } else {
                *dst++ = '%';
                *dst++ = (char)toupper((unsigned char)src[1]);
                *dst++ = (char)toupper((unsigned char)src[2]);
            }
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static void collapse_duplicate_slashes(char *path)
{
    if (!path) {
        return;
    }
    char *src = path;
    char *dst = path;
    int prev_slash = 0;
    while (*src) {
        if (*src == '/') {
            if (!prev_slash) {
                *dst++ = *src;
            }
            prev_slash = 1;
        } else {
            *dst++ = *src;
            prev_slash = 0;
        }
        src++;
    }
    *dst = '\0';
}

char *canonicalize_url(const char *url)
{
    if (!url) {
        return NULL;
    }

    CURLU *cu = curl_url();
    if (!cu) {
        return NULL;
    }

    CURLUcode rc
        = curl_url_set(cu, CURLUPART_URL, url, CURLU_NON_SUPPORT_SCHEME);
    if (rc != CURLUE_OK) {
        curl_url_cleanup(cu);
        return NULL;
    }

    /* Strip URL fragment */
    curl_url_set(cu, CURLUPART_FRAGMENT, NULL, 0);

    /* Lowercase scheme */
    char *scheme = NULL;
    if (curl_url_get(cu, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK) {
        for (char *p = scheme; *p; p++) {
            *p = (char)tolower((unsigned char)*p);
        }
        curl_url_set(cu, CURLUPART_SCHEME, scheme, 0);
        curl_free(scheme);
    }

    /* Lowercase host */
    char *host = NULL;
    if (curl_url_get(cu, CURLUPART_HOST, &host, 0) == CURLUE_OK) {
        for (char *p = host; *p; p++) {
            *p = (char)tolower((unsigned char)*p);
        }
        curl_url_set(cu, CURLUPART_HOST, host, 0);
        curl_free(host);
    }

    /* Normalize path */
    char *path = NULL;
    if (curl_url_get(cu, CURLUPART_PATH, &path, 0) == CURLUE_OK) {
        collapse_duplicate_slashes(path);
        normalize_percent_encoding(path);
        curl_url_set(cu, CURLUPART_PATH, path, CURLU_PATH_AS_IS);
        curl_free(path);
    }

    char *out = NULL;
    rc = curl_url_get(cu, CURLUPART_URL, &out, CURLU_NO_DEFAULT_PORT);
    curl_url_cleanup(cu);

    if (rc != CURLUE_OK || !out) {
        return NULL;
    }

    char *res = STRDUP(out);
    curl_free(out);
    return res;
}

char *url_to_cache_path(const char *url)
{
    if (!url) {
        return NULL;
    }
    char *canonical = canonicalize_url(url);
    char *result = string_to_cache_path(canonical ? canonical : url);
    FREE(canonical);
    return result;
}
