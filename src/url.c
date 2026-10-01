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

char *external_url_to_filename(const char *url)
{
    if (!url) {
        return STRDUP("");
    }
    /*
     * Skip the scheme://host:port/ prefix — walk past the third slash.
     */
    int slashes = 0;
    const char *p = url;
    while (*p && slashes < 3) {
        if (*p == '/') {
            slashes++;
        }
        p++;
    }
    /* p now points to the first character of the path (after the
     * trailing slash of the origin, e.g. "path/file.iso"). */

    if (*p == '\0') {
        /* Root-only URL — no filename to extract. */
        return STRDUP("");
    }

    size_t path_len = strlen(p);
    char *path_copy = STRNDUP(p, path_len);

    /* Strip query string and fragment identifier if present (must be done
     * before trailing slash check).
     */
    char *q = strpbrk(path_copy, "?#");
    if (q) {
        *q = '\0';
    }

    /* Strip trailing slash if present. */
    path_len = strlen(path_copy);
    if (path_len > 0 && path_copy[path_len - 1] == '/') {
        path_copy[path_len - 1] = '\0';
    }

    /* Find the last '/' and take everything after it. */
    char *last_slash = strrchr(path_copy, '/');
    char *filename;
    if (last_slash) {
        filename = STRDUP(last_slash + 1);
    } else {
        filename = STRDUP(path_copy);
    }
    FREE(path_copy);

    /* URL-decode the filename so it can be used as a filesystem name. */
    char *decoded = curl_easy_unescape(NULL, filename, 0, NULL);
    if (!decoded) {
        return filename;
    }
    char *result = STRDUP(decoded);
    curl_free(decoded);
    FREE(filename);
    for (char *ptr = result; *ptr; ptr++) {
        if (*ptr == '/') {
            *ptr = '_';
        }
    }
    return result;
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

char *url_to_cache_path(const char *url)
{
    if (!url) {
        return NULL;
    }

    /*
     * Check if this URL is cross-origin relative to the root table
     */
    if (ROOT_LINK_TBL && ROOT_LINK_TBL->links && ROOT_LINK_TBL->links[0]) {
        const char *root_url = ROOT_LINK_TBL->links[0]->f_url;
        if (is_cross_origin(root_url, url)) {
            /*
             * Cross-origin URL: sanitize into a flat cache filename.
             */
            char *temp = curl_easy_unescape(NULL, url, 0, NULL);
            char *unescaped_path = temp ? STRDUP(temp) : STRDUP(url);
            if (temp) {
                curl_free(temp);
            }
            /* Sanitize ".." to prevent path traversal */
            char *p = unescaped_path;
            while ((p = strstr(p, ".."))) {
                p[0] = '_';
                p[1] = '_';
                p += 2;
            }
            /* Flatten slashes and colons */
            for (char *sp = unescaped_path; *sp; sp++) {
                if (*sp == '/' || *sp == ':') {
                    *sp = '_';
                }
            }
            if (strlen(unescaped_path) > 200) {
                char *hash = generate_md5sum(url);
                if (hash) {
                    unescaped_path[160] = '_';
                    memcpy(unescaped_path + 161, hash, 32);
                    unescaped_path[193] = '\0';
                    FREE(hash);
                }
            }
            return unescaped_path;
        }
    }

    /*
     * Same-origin or no root table: construct cache path from the root
     * of the server itself.
     */
    const char *server_path = get_url_path_from_server_root(url);
    char *temp = curl_easy_unescape(NULL, server_path, 0, NULL);
    char *unescaped_path = temp ? STRDUP(temp) : STRDUP(server_path);
    if (temp) {
        curl_free(temp);
    }

    /* Sanitize unescaped_path to prevent path traversal via ".." */
    char *p = unescaped_path;
    while ((p = strstr(p, ".."))) {
        p[0] = '_';
        p[1] = '_';
        p += 2;
    }

    return unescaped_path;
}

static int hex_char_to_val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

void make_link_relative(const char *page_url, char *link_url)
{
    if (!page_url || !link_url) {
        return;
    }

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
        if (*page_url == '/' && !--slashes_left_to_find) {
            break;
        }
        /* N.B. This is here, rather than doing `while (*page_url++)`, because
           when we're done we want the pointer to point at the final slash. */
        page_url++;
    }
    if (slashes_left_to_find) {
        if (slashes_left_to_find == 1 && !*page_url) {
            /* We're at the top level of the web site and the user entered the
               URL without a trailing slash. */
            page_url = "/";
        } else {
            /* Well, that's odd. Let's return rather than trying to dig
               ourselves deeper into whatever hole we're in. */
            return;
        }
    }

    /* Unescape the page path prefix so we can match against link_url
       regardless of whether spaces/special characters are percent-encoded
       or raw in either URL. */
    char *unescaped_url = curl_easy_unescape(NULL, page_url, 0, NULL);
    const char *url = unescaped_url ? unescaped_url : page_url;
    size_t url_len = strlen(url);

    const char *link_ptr = link_url;
    int matched = 1;
    for (size_t i = 0; i < url_len; i++) {
        unsigned char expected = (unsigned char)url[i];
        if (link_ptr[0] == '%' && isxdigit((unsigned char)link_ptr[1])
            && isxdigit((unsigned char)link_ptr[2])) {
            int hex_val = (hex_char_to_val(link_ptr[1]) << 4)
                          | hex_char_to_val(link_ptr[2]);
            if (hex_val == expected) {
                link_ptr += 3;
                continue;
            }
        }
        if ((unsigned char)link_ptr[0] == expected) {
            link_ptr += 1;
            continue;
        }
        matched = 0;
        break;
    }

    if (matched) {
        if (url_len > 0 && url[url_len - 1] != '/') {
            if (*link_ptr != '/') {
                matched = 0;
            } else {
                link_ptr++;
            }
        }
    }

    if (unescaped_url) {
        curl_free(unescaped_url);
    }

    if (!matched) {
        return;
    }

    /* Move the part of the link URL after the parent page's path to
       the beginning of the link URL string, discarding what came
       before it. */
    size_t skip_len = (size_t)(link_ptr - link_url);
    memmove(link_url, link_url + skip_len, strlen(link_url) - skip_len + 1);
}
