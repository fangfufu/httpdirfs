#ifndef NETWORK_H
#define NETWORK_H

#include "data.h"

#include <gumbo.h>

/** \brief root link table */
extern LinkTable *ROOT_LINK_TBL;

/** \brief Initialise the network module */
void network_init(const char *url);

/** \brief make a new Link */
Link *Link_new();

/** \brief free a Link */
void Link_free(Link *link);

/** \brief download a link */
int Link_download(Link *link, size_t start, size_t end);

/** \brief make a new LinkTable */
LinkTable *LinkTable_new(const char *url);

/** \brief free a LinkTable */
void LinkTable_free(LinkTable *linktbl);

/** \brief add a link to the link table */
void LinkTable_add(LinkTable *linktbl, Link *link);

/**
 * \brief fill the LinkTable
 * \details fill the LinkTable with link type information
 */
void LinkTable_fill(LinkTable *linktbl);

/** \brief print a LinkTable */
void LinkTable_print(LinkTable *linktbl);

/** \brief convert a html page to a LinkTable */
void HTML_to_LinkTable(GumboNode *node, LinkTable *linktbl);

/** \brief find the link associated with a path */
Link *path_to_Link(const char *path, LinkTable *linktbl);

#endif
