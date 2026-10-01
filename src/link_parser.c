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
 * \file link_parser.c
 * \brief HTML directory scraping and link extraction implementation
 */

#include "link_parser.h"

#include "config.h"
#include "link.h"
#include "log.h"
#include "url.h"
#include "util.h"

#include <ctype.h>
#include <gumbo.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>

int link_linknames_equal(const char *str_a, const char *str_b)
{
    if (!str_a || !str_b) {
        return 0;
    }
    size_t len_a = strnlen(str_a, PATH_MAX);
    size_t len_b = strnlen(str_b, PATH_MAX);
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
        if (!strncmp(str_a, str_b, comp_len)) {
            /* If the lengths are equal, they are identical */
            if (len_a == len_b) {
                identical = 1;
            } else {
                /* Otherwise the last character of the longer string should be
                 * '/' */
                const char *longer_str = len_a > len_b ? str_a : str_b;
                identical = (longer_str[comp_len] == '/');
            }
        }
    }

end:
    return identical;
}

struct LinkHashSet {
    const char **buckets;
    int capacity;
    int size;
};

unsigned int link_hash_str(const char *str)
{
    unsigned int hash = 5381;
    int c;
    size_t len = strnlen(str, PATH_MAX);

    /* Strip all trailing slashes */
    while (len > 0 && str[len - 1] == '/') {
        len--;
    }

    for (size_t i = 0; i < len; i++) {
        c = (unsigned char)str[i];
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

LinkHashSet *LinkHashSet_new(int capacity)
{
    if (capacity <= 0) {
        capacity = 16;
    } else {
        int power = 1;
        while (power < capacity && power < (1 << 30)) {
            power <<= 1;
        }
        capacity = power;
    }
    LinkHashSet *set = (LinkHashSet *)CALLOC(1, sizeof(LinkHashSet));
    set->capacity = capacity;
    set->buckets = (const char **)CALLOC(capacity, sizeof(const char *));
    return set;
}

static void LinkHashSet_resize(LinkHashSet *set)
{
    if (set->capacity <= 0) {
        return;
    }
    int old_capacity = set->capacity;
    const char **old_buckets = set->buckets;
    if (set->capacity > INT_MAX / 2) {
        lprintf(fatal, "LinkHashSet capacity overflow\n");
    }
    set->capacity *= 2;
    set->buckets = (const char **)CALLOC(set->capacity, sizeof(const char *));

    for (int i = 0; i < old_capacity; i++) {
        if (old_buckets[i]) {
            unsigned int hash = link_hash_str(old_buckets[i]);
            int bucket = hash & (set->capacity - 1);
            while (set->buckets[bucket] != NULL) {
                bucket = (bucket + 1) & (set->capacity - 1);
            }
            set->buckets[bucket] = old_buckets[i];
        }
    }
    FREE(old_buckets);
}

int LinkHashSet_add(LinkHashSet *set, const char *linkname)
{
    if (!set || !linkname || set->capacity <= 0) {
        return 0;
    }
    if (set->size >= set->capacity / 2) {
        LinkHashSet_resize(set);
    }
    unsigned int hash = link_hash_str(linkname);
    int bucket = hash & (set->capacity - 1);
    while (set->buckets[bucket] != NULL) {
        if (link_linknames_equal(set->buckets[bucket], linkname)) {
            return 0;
        }
        bucket = (bucket + 1) & (set->capacity - 1);
    }
    set->buckets[bucket] = STRDUP(linkname);
    set->size++;
    return 1;
}

void LinkHashSet_free(LinkHashSet *set)
{
    if (!set) {
        return;
    }
    for (int i = 0; i < set->capacity; i++) {
        if (set->buckets[i]) {
            FREE(set->buckets[i]);
        }
    }
    FREE(set->buckets);
    FREE(set);
}

static void collect_gumbo_text(const GumboNode *node, char **buf, size_t *len,
                               size_t *cap)
{
    if (!node) {
        return;
    }
    if (node->type == GUMBO_NODE_TEXT || node->type == GUMBO_NODE_WHITESPACE) {
        const char *t = node->v.text.text;
        if (t) {
            size_t tlen = strlen(t);
            if (*len + tlen + 1 > *cap) {
                *cap = (*cap == 0) ? 256 : ((*cap * 2) + tlen + 1);
                *buf = (char *)REALLOC(*buf, *cap);
            }
            memcpy(*buf + *len, t, tlen);
            *len += tlen;
            (*buf)[*len] = '\0';
        }
    } else if (node->type == GUMBO_NODE_ELEMENT) {
        const GumboVector *children = &node->v.element.children;
        for (size_t i = 0; i < children->length; ++i) {
            collect_gumbo_text((const GumboNode *)children->data[i], buf, len,
                               cap);
        }
    }
}

char *extract_anchor_text(const GumboNode *node)
{
    if (!node) {
        return STRDUP("");
    }
    char *raw = NULL;
    size_t len = 0;
    size_t cap = 0;
    collect_gumbo_text(node, &raw, &len, &cap);
    if (!raw || len == 0) {
        FREE(raw);
        return STRDUP("");
    }

    char *src = raw;
    char *dst = raw;
    int in_space = 0;

    /* Skip leading whitespace */
    int sp = 0;
    while ((sp = check_space(src)) > 0) {
        src += sp;
    }

    while (*src) {
        sp = check_space(src);
        if (sp > 0) {
            if (!in_space) {
                *dst++ = ' ';
                in_space = 1;
            }
            src += sp;
            continue;
        }
        unsigned char c = (unsigned char)*src;
        if (c == '/') {
            *dst++ = '_';
            in_space = 0;
        } else if (c >= 32 && c != 127) {
            *dst++ = (char)c;
            in_space = 0;
        }
        src++;
    }

    /* Strip trailing whitespace */
    while (dst > raw && isspace((unsigned char)*(dst - 1))) {
        dst--;
    }
    *dst = '\0';

    char *result = STRDUP(raw);
    FREE(raw);
    return result;
}

char *generate_collision_free_name(LinkHashSet *set, const char *anchor,
                                   char **segments, int num_segments)
{
    if (!set) {
        return NULL;
    }

    /* Strip preceding dots and whitespace simultaneously so that prepending
     * the anchor will not leave leading dots that cause items to be hidden */
    const char *clean_anchor = anchor;
    if (clean_anchor) {
        while (*clean_anchor == '.' || isspace((unsigned char)*clean_anchor)) {
            clean_anchor++;
        }
    }

    char candidate[NAME_MAX + 1];

    if (num_segments <= 0) {
        if (!clean_anchor || *clean_anchor == '\0') {
            return NULL;
        }
        snprintf(candidate, sizeof(candidate), "%s", clean_anchor);
        if (LinkHashSet_add(set, candidate)) {
            return STRDUP(candidate);
        }
        for (int suffix = 1; suffix < 10000; suffix++) {
            char suffix_str[32];
            int s_len = snprintf(suffix_str, sizeof(suffix_str), "-%d", suffix);
            char suffixed[NAME_MAX + 1];
            size_t base_len = strlen(candidate);
            if (base_len + (size_t)s_len >= sizeof(suffixed)) {
                base_len = sizeof(suffixed) - (size_t)s_len - 1;
            }
            snprintf(suffixed, sizeof(suffixed), "%.*s%s", (int)base_len,
                     candidate, suffix_str);
            if (LinkHashSet_add(set, suffixed)) {
                return STRDUP(suffixed);
            }
        }
        return NULL;
    }

    /* Iterate through path depths i = 1, 2, ..., num_segments */
    for (int i = 1; i <= num_segments; i++) {
        char path_part[NAME_MAX + 1];
        path_part[0] = '\0';
        size_t cur_len = 0;

        for (int j = num_segments - i; j < num_segments; j++) {
            const char *seg = segments[j];
            size_t seg_len = strlen(seg);
            if (cur_len > 0) {
                if (cur_len + 1 < sizeof(path_part)) {
                    path_part[cur_len++] = '-';
                    path_part[cur_len] = '\0';
                }
            }
            if (cur_len + seg_len < sizeof(path_part)) {
                memcpy(path_part + cur_len, seg, seg_len);
                cur_len += seg_len;
                path_part[cur_len] = '\0';
            }
        }

        const char *last_seg = segments[num_segments - 1];
        int omit_anchor = (!clean_anchor || *clean_anchor == '\0');
        if (i == 1 && clean_anchor && *clean_anchor != '\0'
            && strcasecmp(clean_anchor, last_seg) == 0) {
            omit_anchor = 1;
        }

        if (omit_anchor) {
            snprintf(candidate, sizeof(candidate), "%s", path_part);
        } else {
            snprintf(candidate, sizeof(candidate), "%s-%s", clean_anchor,
                     path_part);
        }

        if (LinkHashSet_add(set, candidate)) {
            return STRDUP(candidate);
        }
    }

    /* Exhaustion fallback: append numeric suffix */
    char last_candidate[NAME_MAX + 1];
    snprintf(last_candidate, sizeof(last_candidate), "%s", candidate);
    for (int suffix = 1; suffix < 10000; suffix++) {
        char suffix_str[32];
        int s_len = snprintf(suffix_str, sizeof(suffix_str), "-%d", suffix);
        char suffixed[NAME_MAX + 1];
        size_t base_len = strlen(last_candidate);
        if (base_len + (size_t)s_len >= sizeof(suffixed)) {
            base_len = sizeof(suffixed) - (size_t)s_len - 1;
        }
        snprintf(suffixed, sizeof(suffixed), "%.*s%s", (int)base_len,
                 last_candidate, suffix_str);
        if (LinkHashSet_add(set, suffixed)) {
            return STRDUP(suffixed);
        }
    }

    return NULL;
}

int is_html_content_type(const char *ct)
{
    if (!ct) {
        return 0;
    }
    while (*ct && isspace((unsigned char)*ct)) {
        ct++;
    }
    if (strncasecmp(ct, "text/html", 9) == 0) {
        char next = ct[9];
        return (next == '\0' || next == ';' || isspace((unsigned char)next));
    }
    return 0;
}

static void process_anchor_node(const char *url, const GumboNode *node,
                                LinkTable *linktbl, LinkHashSet *set,
                                LinkHashSet *target_url_set)
{
    GumboAttribute *href
        = gumbo_get_attribute(&node->v.element.attributes, "href");
    if (!href) {
        return;
    }
    const char *raw_href = href->value;

    if (CONFIG.ignore_anchors && raw_href[0] == '#') {
        /* Skip intra-page HTML anchor / fragment links when requested */
        return;
    }

    char target_url[PATH_MAX + 1];
    int target_url_resolved
        = resolve_target_url(url, raw_href, target_url, sizeof(target_url));
    if (target_url_resolved && is_ancestor_head_link(linktbl, target_url)) {
        return;
    }

    if (CONFIG.advanced_parsing_mode) {
        if (target_url_resolved) {
            int allow = 1;
            if (CONFIG.same_origin_only) {
                const char *page_url = NULL;
                if (ROOT_LINK_TBL && ROOT_LINK_TBL->links
                    && ROOT_LINK_TBL->links[0]) {
                    page_url = ROOT_LINK_TBL->links[0]->f_url;
                } else if (linktbl && linktbl->links && linktbl->links[0]) {
                    page_url = linktbl->links[0]->f_url;
                } else {
                    page_url = url;
                }
                if (page_url && is_cross_origin(page_url, target_url)) {
                    allow = 0;
                }
            }
            if (allow) {
                /* Early duplicate target link removal (first anchor text
                 * wins) */
                if (!target_url_set
                    || LinkHashSet_add(target_url_set, target_url)) {
                    char *anchor = extract_anchor_text(node);
                    char **segments = NULL;
                    int num_segments = 0;
                    extract_url_path_segments(target_url, &segments,
                                              &num_segments);

                    char *linkname = generate_collision_free_name(
                        set, anchor, segments, num_segments);
                    FREE(anchor);
                    free_url_path_segments(segments, num_segments);

                    if (linkname && linkname[0] != '\0') {
                        const char *qf = strpbrk(target_url, "?#");
                        size_t tlen = qf ? (size_t)(qf - target_url)
                                         : strlen(target_url);
                        LinkType type
                            = (tlen > 0 && target_url[tlen - 1] == '/')
                                  ? LINK_UNINITIALISED_DIR
                                  : LINK_UNINITIALISED_FILE;
                        Link *link = Link_new(linkname, type);
                        snprintf(link->f_url, sizeof(link->f_url), "%s",
                                 target_url);
                        LinkTable_add(linktbl, link);
                        FREE(linkname);
                    }
                }
            }
        }
    } else if (CONFIG.external_links && is_external_url(raw_href)
               && is_cross_origin(url, raw_href)) {
        /*
         * -------- External (cross-origin) link handling --------
         * Extract the filename from the external URL and create a
         * Link with f_url already pointing to the external server.
         * LinkTable_fill() will skip URL-construction for these.
         */
        char *filename = external_url_to_filename(raw_href);
        if (filename && filename[0] != '\0' && strcmp(filename, ".") != 0
            && strcmp(filename, "..") != 0) {
            /* Determine type: directory if URL (ignoring query/fragment)
             * ends with '/' */
            const char *qf = strpbrk(raw_href, "?#");
            size_t href_len = qf ? (size_t)(qf - raw_href) : strlen(raw_href);
            LinkType type = (href_len > 0 && raw_href[href_len - 1] == '/')
                                ? LINK_UNINITIALISED_DIR
                                : LINK_UNINITIALISED_FILE;

            /* First-wins: skip if a link with this name already exists */
            if (LinkHashSet_add(set, filename)) {
                Link *link = Link_new(filename, type);
                if (target_url_resolved) {
                    snprintf(link->f_url, sizeof(link->f_url), "%s",
                             target_url);
                } else {
                    snprintf(link->f_url, sizeof(link->f_url), "%s", raw_href);
                }
                LinkTable_add(linktbl, link);
            }
        }
        FREE(filename);
    } else {
        /*
         * -------- Same-origin / relative link handling (unchanged)
         * --------
         */
        char *relative_url = STRNDUP(raw_href, PATH_MAX);
        make_link_relative(url, relative_url);

        /* Truncate at the first slash to support links to subdirectories */
        char *slash = strchr(relative_url, '/');
        if (slash && slash != relative_url) {
            /* Don't truncate full URIs like http://... */
            if (*(slash - 1) != ':' && slash[1] != '/') {
                slash[1] = '\0';
            }
        }

        /* if it is valid, copy the link onto the heap */
        LinkType type = linkname_to_LinkType(relative_url);

        /* Check if the new link is a duplicate */
        if ((type == LINK_UNINITIALISED_DIR)
            || (type == LINK_UNINITIALISED_FILE)) {
            if (LinkHashSet_add(set, relative_url)) {
                LinkTable_add(linktbl, Link_new(relative_url, type));
            }
        }
        FREE(relative_url);
    }
}

/**
 * Recursively walk the HTML DOM tree to extract links into the link table.
 */
static void HTML_to_LinkTable(const char *url, GumboNode *node,
                              LinkTable *linktbl, LinkHashSet *set,
                              LinkHashSet *target_url_set)
{
    if (node->type != GUMBO_NODE_ELEMENT) {
        return;
    }

    if (node->v.element.tag == GUMBO_TAG_A) {
        process_anchor_node(url, node, linktbl, set, target_url_set);
    }

    GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; ++i) {
        HTML_to_LinkTable(url, (GumboNode *)children->data[i], linktbl, set,
                          target_url_set);
    }
}

void LinkTable_parse_html(LinkTable *linktbl, const char *url, const char *html)
{
    GumboOutput *output = gumbo_parse(html);
    LinkHashSet *set = LinkHashSet_new(linktbl->size * 2);
    LinkHashSet *target_url_set = CONFIG.advanced_parsing_mode
                                      ? LinkHashSet_new(linktbl->size * 2)
                                      : NULL;

    HTML_to_LinkTable(url, output->root, linktbl, set, target_url_set);

    if (target_url_set) {
        LinkHashSet_free(target_url_set);
    }
    LinkHashSet_free(set);
    gumbo_destroy_output(&kGumboDefaultOptions, output);
}
