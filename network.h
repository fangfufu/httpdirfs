#ifndef NETWORK_H
#define NETWORK_H

#include "data.h"

#include <gumbo.h>

/** \brief Initialise the network module */
void network_init(const char *url);

/**
 * \brief download a link */
/* \return the number of bytes downloaded
 */
size_t Link_download(Link *link, off_t offset, size_t size);

/** \brief print a LinkTable */
void LinkTable_print(LinkTable *linktbl);

/** \brief find the link associated with a path */
Link *path_to_Link(const char *path);

#endif
