#ifndef LINK_H
#define LINK_H
#include <stdio.h>
#include <stdlib.h>

#include <gumbo.h>
/* \brief the link type */
typedef enum {
    LINK_DIR,
    LINK_FILE,
    LINK_UNKNOWN
} linktype;

/* \brief link list data type */
typedef struct {
    int num;
    char **link;
    linktype *type;
} ll_t;

/* \brief make a new link list */
ll_t *linklist_new();

/* \brief print a link list */
void linklist_print(ll_t *links);

/* \brief convert a html page to a link list */
void html_to_linklist(GumboNode *node, ll_t *links);

/* \brief free a link list */
void linklist_free(ll_t *links);

/* \brief the upper level */
/* \warning does not check if you have reached the base level! */
char *url_upper(const char *url);

/* \brief append url */
char *url_append(const char *url, const char *sublink);

#endif
