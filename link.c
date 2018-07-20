#include <ctype.h>

#include "link.h"
#include "string.h"

Link *Link_new(const char *p_url)
{
    Link *link = calloc(1, sizeof(Link));

    size_t p_url_len = strnlen(p_url, LINK_LEN_MAX) + 1;
    link->p_url = malloc(p_url_len);
    link->p_url = strncpy(link->p_url, p_url, p_url_len);

    link->type = LINK_UNKNOWN;
    link->curl_h = curl_easy_init();
    link->res = -1;
    link->data = NULL;
    link->data_sz = 0;

    return link;
}

void Link_free(Link *link)
{
    free(link->p_url);
    curl_easy_cleanup(link->curl_h);
    free(link->data);
    free(link);
    link = NULL;
}

LinkTable *LinkTable_new()
{
    LinkTable *linktbl = calloc(1, sizeof(LinkTable));
    linktbl->num = 0;
    linktbl->links = NULL;
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

void LinkTable_print(LinkTable *linktbl)
{
    for (int i = 0; i < linktbl->num; i++) {
        printf("%d %c %s\n",
               i,
               linktbl->links[i]->type,
               linktbl->links[i]->p_url);
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
    char *str = malloc(len* sizeof(char));
    strncpy(str, url, len);
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
