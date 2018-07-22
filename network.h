#ifndef NETWORK_H
#define NETWORK_H

#include "data.h"

#include <gumbo.h>

/** \brief root link table */
extern LinkTable *ROOT_LINK_TBL;

/** \brief Initialise the network module */
void network_init(const char *url);

/** \brief download a link */
int Link_download(Link *link, size_t start, size_t end);

/** \brief print a LinkTable */
void LinkTable_print(LinkTable *linktbl);

/** \brief find the link associated with a path */
Link *path_to_Link(const char *path, LinkTable *linktbl);

#endif
