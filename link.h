#ifndef LINK_H
#define LINK_H
#include <stdio.h>
#include <stdlib.h>

#include <gumbo.h>

#include "data.h"

/** \brief make a new Link */
Link *Link_new();

/** \brief free a Link */
void Link_free(Link *link);

/** \brief make a new LinkTable */
LinkTable *LinkTable_new();

/** \brief free a LinkTable */
void LinkTable_free(LinkTable *linktbl);

/** \brief add a link to the link table */
void LinkTable_add(LinkTable *linktbl, Link *link);

/** \brief print a LinkTable */
void LinkTable_print(LinkTable *linktbl);

/** \brief convert a html page to a LinkTable */
void HTML_to_LinkTable(GumboNode *node, LinkTable *linktbl);

/** \brief the upper level */
/* \warning does not check if you have reached the base level! */
char *url_upper(const char *url);

/** \brief append url */
char *url_append(const char *url, const char *sublink);

#endif
