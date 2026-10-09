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

    /* Assuming that the shorter string has a non-zero length and that the
     * common parts of the strings are the same */
    if (comp_len && !strncmp(str_a, str_b, comp_len)) {
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

int LinkHashSet_contains(LinkHashSet *set, const char *linkname)
{
    if (!set || !linkname || set->capacity <= 0) {
        return 0;
    }
    unsigned int hash = link_hash_str(linkname);
    int bucket = hash & (set->capacity - 1);
    for (int probe = 0; probe < set->capacity; probe++) {
        int b = (bucket + probe) & (set->capacity - 1);
        if (!set->buckets[b]) {
            return 0;
        }
        if (link_linknames_equal(set->buckets[b], linkname)) {
            return 1;
        }
    }
    return 0;
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
            if (tlen > SIZE_MAX - *len - 1) {
                return;
            }
            size_t needed = *len + tlen + 1;
            if (needed > *cap) {
                size_t new_cap = (*cap == 0) ? 256 : *cap;
                while (new_cap < needed) {
                    if (new_cap > SIZE_MAX / 2) {
                        new_cap = needed;
                        break;
                    }
                    new_cap *= 2;
                }
                *buf = (char *)REALLOC(*buf, new_cap);
                *cap = new_cap;
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

/**
 * \brief Clean a raw anchor string in place into a filesystem-friendly name.
 *
 * Collapses whitespace, replaces '/' with '_', drops non-printable
 * characters, and trims leading/trailing whitespace and slashes.
 * \param raw The raw string (ownership is taken; it is freed internally).
 * \param len The initial length of \p raw.
 * \return A newly allocated cleaned string (never NULL).
 */
static char *clean_anchor_string(char *raw, size_t len)
{
    if (!raw || len == 0) {
        FREE(raw);
        return STRDUP("");
    }

    /* Strip trailing whitespace and slashes */
    while (len > 0
           && (isspace((unsigned char)raw[len - 1]) || raw[len - 1] == '/')) {
        len--;
        raw[len] = '\0';
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
        /* Collapse newlines and tabs (formatting whitespace) into a single
         * space */
        if (*src == '\r' || *src == '\n' || *src == '\t') {
            if (!in_space) {
                *dst++ = ' ';
                in_space = 1;
            }
            src++;
            while (*src && check_space(src) > 0) {
                src += check_space(src);
            }
            continue;
        }
        sp = check_space(src);
        if (sp > 0) {
            *dst++ = ' ';
            src += sp;
            in_space = 0;
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

char *extract_anchor_text(const GumboNode *node)
{
    if (!node) {
        return STRDUP("");
    }
    char *raw = NULL;
    size_t len = 0;
    size_t cap = 0;
    collect_gumbo_text(node, &raw, &len, &cap);
    return clean_anchor_string(raw, len);
}

/**
 * \brief Derive a naming anchor from an <img> alt attribute.
 *
 * The alt text is used only when it is non-empty after cleaning and is not
 * shared by multiple images on the same page (a duplicated alt is treated as
 * generic filler).
 * \param node The GumboNode of an img element.
 * \param alt_dups Set of alt strings that appear more than once on the page.
 * \return Newly allocated cleaned anchor, or NULL for filename-only naming.
 */
static char *img_alt_anchor(const GumboNode *node, LinkHashSet *alt_dups)
{
    GumboAttribute *alt
        = gumbo_get_attribute(&node->v.element.attributes, "alt");
    if (!alt || !alt->value) {
        return NULL;
    }
    char *cleaned = clean_anchor_string(STRDUP(alt->value), strlen(alt->value));
    if (cleaned[0] == '\0') {
        FREE(cleaned);
        return NULL;
    }
    if (alt_dups && LinkHashSet_contains(alt_dups, cleaned)) {
        FREE(cleaned);
        return NULL;
    }
    return cleaned;
}

static int anchor_matches_segment(const char *anchor, const char *seg)
{
    if (!anchor || !seg) {
        return 0;
    }
    const unsigned char *a = (const unsigned char *)anchor;
    const unsigned char *s = (const unsigned char *)seg;
    while (*a && *s) {
        if (isspace(*a) && isspace(*s)) {
            while (isspace(*a)) {
                a++;
            }
            while (isspace(*s)) {
                s++;
            }
            continue;
        }
        if (tolower(*a) != tolower(*s)) {
            return 0;
        }
        a++;
        s++;
    }
    while (isspace(*a)) {
        a++;
    }
    while (isspace(*s)) {
        s++;
    }
    return (*a == '\0' && *s == '\0');
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
            if (cur_len > 0 && cur_len + 1 < sizeof(path_part)) {
                path_part[cur_len++] = '-';
                path_part[cur_len] = '\0';
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
            && anchor_matches_segment(clean_anchor, last_seg)) {
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

/**
 * \brief Resolve a raw reference and add it to the link table if new.
 *
 * Shared by every reference kind (anchors and media resource elements).
 * \param url The page URL the reference is relative to.
 * \param raw_ref The raw attribute value (href / src / data / srcset
 * candidate).
 * \param anchor Optional cleaned naming hint (anchor text or img alt).
 * NULL or empty falls back to URL-derived naming.
 */
static void process_link_ref(const char *url, const char *raw_ref,
                             const char *anchor, LinkTable *linktbl,
                             LinkHashSet *set, LinkHashSet *target_url_set)
{
    if (!raw_ref || raw_ref[0] == '\0') {
        return;
    }

    if (CONFIG.ignore_anchors && raw_ref[0] == '#') {
        /* Skip intra-page HTML anchor / fragment links when requested */
        return;
    }

    char target_url[PATH_MAX + 1];
    int target_url_resolved
        = resolve_target_url(url, raw_ref, target_url, sizeof(target_url));
    if (!target_url_resolved || is_ancestor_head_link(linktbl, target_url)) {
        return;
    }

    if (!CONFIG.allow_external_origin) {
        const char *page_url = NULL;
        if (ROOT_LINK_TBL && ROOT_LINK_TBL->links && ROOT_LINK_TBL->links[0]) {
            page_url = ROOT_LINK_TBL->links[0]->f_url;
        } else if (linktbl && linktbl->links && linktbl->links[0]) {
            page_url = linktbl->links[0]->f_url;
        } else {
            page_url = url;
        }
        if (page_url && is_cross_origin(page_url, target_url)) {
            return;
        }
    }

    /* Early duplicate target link removal (first reference wins) */
    if (!target_url_set || LinkHashSet_add(target_url_set, target_url)) {
        char **segments = NULL;
        int num_segments = 0;
        extract_url_path_segments(target_url, &segments, &num_segments);

        char *linkname
            = generate_collision_free_name(set, anchor, segments, num_segments);
        free_url_path_segments(segments, num_segments);

        if (linkname && linkname[0] != '\0') {
            const char *qf = strpbrk(target_url, "?#");
            size_t tlen = qf ? (size_t)(qf - target_url) : strlen(target_url);
            LinkType type = (tlen > 0 && target_url[tlen - 1] == '/')
                                ? LINK_UNINITIALISED_DIR
                                : LINK_UNINITIALISED_FILE;
            Link *link = Link_new(linkname, type);
            snprintf(link->f_url, sizeof(link->f_url), "%s", target_url);
            LinkTable_add(linktbl, link);
            FREE(linkname);
        }
    }
}

/**
 * \brief Process every candidate of a srcset attribute value.
 *
 * Format: "url [descriptor] (, url [descriptor] ...)*" where the descriptor
 * is a width ("480w") or scale ("2x") hint. Each URL becomes a link.
 */
static void process_srcset_ref(const char *url, const char *srcset,
                               const char *anchor, LinkTable *linktbl,
                               LinkHashSet *set, LinkHashSet *target_url_set)
{
    if (!srcset || *srcset == '\0') {
        return;
    }
    char candidate[PATH_MAX + 1];
    const char *p = srcset;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        size_t n = 0;
        while (*p && !isspace((unsigned char)*p) && *p != ','
               && n + 1 < sizeof(candidate)) {
            candidate[n++] = *p++;
        }
        candidate[n] = '\0';
        if (n > 0) {
            process_link_ref(url, candidate, anchor, linktbl, set,
                             target_url_set);
        }
        /* Skip the width / scale descriptor up to the next candidate */
        while (*p && *p != ',') {
            p++;
        }
        if (*p == ',') {
            p++;
        }
    }
}

/**
 * \brief Process a single named attribute of an element as a link reference.
 */
static void process_element_attr(const char *url, const GumboElement *el,
                                 const char *attr_name, const char *anchor,
                                 LinkTable *linktbl, LinkHashSet *set,
                                 LinkHashSet *target_url_set)
{
    GumboAttribute *attr = gumbo_get_attribute(&el->attributes, attr_name);
    if (attr && attr->value) {
        process_link_ref(url, attr->value, anchor, linktbl, set,
                         target_url_set);
    }
}

/**
 * \brief Process the link-bearing attributes of a resource element.
 *
 * In addition to \p attr_name, a srcset attribute (img / source) is
 * expanded into its individual candidates.
 */
static void process_resource_element(const char *url, const GumboElement *el,
                                     const char *attr_name, const char *anchor,
                                     LinkTable *linktbl, LinkHashSet *set,
                                     LinkHashSet *target_url_set)
{
    process_element_attr(url, el, attr_name, anchor, linktbl, set,
                         target_url_set);
    if (el->tag == GUMBO_TAG_IMG || el->tag == GUMBO_TAG_SOURCE) {
        GumboAttribute *srcset = gumbo_get_attribute(&el->attributes, "srcset");
        if (srcset && srcset->value) {
            process_srcset_ref(url, srcset->value, anchor, linktbl, set,
                               target_url_set);
        }
    }
}

/**
 * \brief Collect img alt strings that occur more than once on the page.
 *
 * A repeated alt text is treated as generic filler and must not be used as
 * a naming anchor (it would produce colliding or misleading names).
 */
static void collect_duplicate_alts(GumboNode *node, LinkHashSet *alt_seen,
                                   LinkHashSet *alt_dups)
{
    if (node->type != GUMBO_NODE_ELEMENT) {
        return;
    }
    if (node->v.element.tag == GUMBO_TAG_IMG) {
        GumboAttribute *alt
            = gumbo_get_attribute(&node->v.element.attributes, "alt");
        if (alt && alt->value) {
            char *cleaned
                = clean_anchor_string(STRDUP(alt->value), strlen(alt->value));
            if (cleaned[0] != '\0' && !LinkHashSet_add(alt_seen, cleaned)) {
                LinkHashSet_add(alt_dups, cleaned);
            }
            FREE(cleaned);
        }
    }
    GumboVector *children = &node->v.element.children;
    for (size_t i = 0; i < children->length; ++i) {
        collect_duplicate_alts((GumboNode *)children->data[i], alt_seen,
                               alt_dups);
    }
}

/**
 * \brief Process the link-bearing attributes of a non-anchor resource
 * element (<area>, <img>, <video>, <script>, <link>, ...).
 */
static void process_resource_node(const char *url, const GumboNode *node,
                                  const GumboElement *el, LinkTable *linktbl,
                                  LinkHashSet *set, LinkHashSet *target_url_set,
                                  LinkHashSet *alt_dups)
{
    GumboAttribute *attr = NULL;

    switch (el->tag) {
    case GUMBO_TAG_AREA:
        attr = gumbo_get_attribute(&el->attributes, "href");
        if (attr && attr->value) {
            char *anchor = extract_anchor_text(node);
            process_link_ref(url, attr->value, anchor, linktbl, set,
                             target_url_set);
            FREE(anchor);
        }
        break;
    case GUMBO_TAG_IMG: {
        char *anchor = img_alt_anchor(node, alt_dups);
        process_resource_element(url, el, "src", anchor, linktbl, set,
                                 target_url_set);
        FREE(anchor);
        break;
    }
    case GUMBO_TAG_SOURCE:
    case GUMBO_TAG_VIDEO:
    case GUMBO_TAG_AUDIO:
        process_resource_element(url, el, "src", NULL, linktbl, set,
                                 target_url_set);
        break;
    case GUMBO_TAG_SCRIPT:
        process_element_attr(url, el, "src", NULL, linktbl, set,
                             target_url_set);
        break;
    case GUMBO_TAG_LINK:
        process_element_attr(url, el, "href", NULL, linktbl, set,
                             target_url_set);
        break;
    case GUMBO_TAG_IFRAME:
    case GUMBO_TAG_FRAME:
        process_element_attr(url, el, "src", NULL, linktbl, set,
                             target_url_set);
        break;
    case GUMBO_TAG_OBJECT:
        process_element_attr(url, el, "data", NULL, linktbl, set,
                             target_url_set);
        break;
    case GUMBO_TAG_EMBED:
    case GUMBO_TAG_TRACK:
        process_element_attr(url, el, "src", NULL, linktbl, set,
                             target_url_set);
        break;
    case GUMBO_TAG_INPUT:
        attr = gumbo_get_attribute(&el->attributes, "type");
        if (attr && attr->value && strcasecmp(attr->value, "image") == 0) {
            process_element_attr(url, el, "src", NULL, linktbl, set,
                                 target_url_set);
        }
        break;
    default:
        break;
    }
}

/**
 * Recursively walk the HTML DOM tree to extract anchor links and media
 * resource references into the link table.
 */
static void HTML_to_LinkTable(const char *url, GumboNode *node,
                              LinkTable *linktbl, LinkHashSet *set,
                              LinkHashSet *target_url_set,
                              LinkHashSet *alt_dups)
{
    if (node->type != GUMBO_NODE_ELEMENT) {
        return;
    }

    const GumboElement *el = &node->v.element;
    GumboAttribute *attr = NULL;

    /* Only <a href> hyperlinks are extracted in normal mode; media and
     * asset references (img / video / script / ...) are materialized only
     * under --website-mode. */
    if (el->tag == GUMBO_TAG_A) {
        attr = gumbo_get_attribute(&el->attributes, "href");
        if (attr && attr->value) {
            char *anchor = extract_anchor_text(node);
            process_link_ref(url, attr->value, anchor, linktbl, set,
                             target_url_set);
            FREE(anchor);
        }
    } else if (CONFIG.website_mode) {
        process_resource_node(url, node, el, linktbl, set, target_url_set,
                              alt_dups);
    }

    const GumboVector *children = &el->children;
    for (unsigned int i = 0; i < children->length; ++i) {
        HTML_to_LinkTable(url, (GumboNode *)children->data[i], linktbl, set,
                          target_url_set, alt_dups);
    }
}

void LinkTable_parse_html(LinkTable *linktbl, const char *url, const char *html)
{
    GumboOutput *output = gumbo_parse(html);
    LinkHashSet *set = LinkHashSet_new(linktbl->size * 2);
    LinkHashSet *target_url_set = LinkHashSet_new(linktbl->size * 2);

    /* First pass: find img alt texts that are reused on the page, so that
     * duplicated alts are not used as naming anchors by any image. Only
     * needed in --website-mode, where <img> references are extracted. */
    LinkHashSet *alt_seen = NULL;
    LinkHashSet *alt_dups = NULL;
    if (CONFIG.website_mode) {
        alt_seen = LinkHashSet_new(16);
        alt_dups = LinkHashSet_new(16);
        collect_duplicate_alts(output->root, alt_seen, alt_dups);
    }

    HTML_to_LinkTable(url, output->root, linktbl, set, target_url_set,
                      alt_dups);

    LinkHashSet_free(alt_dups);
    LinkHashSet_free(alt_seen);
    LinkHashSet_free(target_url_set);
    LinkHashSet_free(set);
    gumbo_destroy_output(&kGumboDefaultOptions, output);
}
