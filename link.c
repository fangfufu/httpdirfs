#include <ctype.h>

#include "link.h"
#include "string.h"

static char linktype_to_char(linktype t)
{
    switch (t) {
        case LINK_DIR :
            return 'D';
        case LINK_FILE :
            return 'F';
        case LINK_UNKNOWN :
            return 'U';
        default :
            return 'E';
    }
}

void linklist_print(ll_t *links)
{
    for (int i = 0; i < links->num; i++) {
        fprintf(stderr, "%d %c %s\n",
                i,
                linktype_to_char(links->type[i]),
                links->link[i]);
    }
}

ll_t *linklist_new()
{
    ll_t *links = malloc(sizeof(ll_t));
    links->num = 0;
    links->link = NULL;
    links->type = NULL;
    return links;
}

static int is_valid_link(const char *n)
{
    /* The link name has to start with alphanumerical character */
    if (!isalnum(n[0])) {
        return 0;
    }
    /* check for http:// and https:// */
    int c = strlen(n);
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
void html_to_linklist(GumboNode *node, ll_t *links)
{
    if (node->type != GUMBO_NODE_ELEMENT) {
        return;
    }
    GumboAttribute* href;

    if (node->v.element.tag == GUMBO_TAG_A &&
        (href = gumbo_get_attribute(&node->v.element.attributes, "href"))) {
        /* if it is valid, copy the link onto the heap */
        if (is_valid_link(href->value)) {
            links->num++;
            links->link = realloc(links->link, links->num * sizeof(char *));
            links->type = realloc(links->type, links->num * sizeof(linktype *));
            int i = links->num - 1;
            links->link[i] = malloc(strlen(href->value) * sizeof(char *));
            strcpy(links->link[i], href->value);
            links->type[i] = LINK_UNKNOWN;
        }
    }

    /* Note the recursive call, lol. */
    GumboVector *children = &node->v.element.children;
    for (size_t i = 0; i < children->length; ++i) {
        html_to_linklist((GumboNode*)children->data[i], links);
    }
    return;
}

void linklist_free(ll_t *links)
{
    for (int i = 0; i < links->num; i++) {
        free(links->link[i]);
    }
    free(links->type);
    free(links);
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
