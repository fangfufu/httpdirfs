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

#ifndef LINK_PARSER_H
#define LINK_PARSER_H

/**
 * \file link_parser.h
 * \brief HTML directory scraping and link extraction header
 */

#include <gumbo.h>
#include <stddef.h>

typedef struct LinkTable LinkTable;
typedef struct LinkHashSet LinkHashSet;

/**
 * \brief Check if two link names are equal, normalizing any single trailing
 * slash.
 * \param str_a The first link name string to compare.
 * \param str_b The second link name string to compare.
 * \return 1 if they are equivalent, 0 otherwise.
 */
int link_linknames_equal(const char *str_a, const char *str_b);

/**
 * \brief Generate a hash value for a link name, ignoring any trailing slashes.
 * \param str The link name string to hash.
 * \return The generated unsigned int hash value.
 */
unsigned int link_hash_str(const char *str);

/**
 * \brief Create a new LinkHashSet with a specified initial capacity.
 * \param capacity The initial number of buckets to allocate.
 * \return Pointer to the newly allocated LinkHashSet.
 */
LinkHashSet *LinkHashSet_new(int capacity);

/**
 * \brief Add a link name to the LinkHashSet if it is not already present.
 * \param set The LinkHashSet to insert the link name into.
 * \param linkname The link name string to add.
 * \return 1 if successfully added (not a duplicate), 0 if it is a duplicate.
 */
int LinkHashSet_add(LinkHashSet *set, const char *linkname);

/**
 * \brief Free all memory allocated for a LinkHashSet.
 * \param set The LinkHashSet to deallocate.
 */
void LinkHashSet_free(LinkHashSet *set);

/**
 * \brief Extract plain anchor text from a GumboNode anchor element.
 */
char *extract_anchor_text(const GumboNode *node);

/**
 * \brief Generate a collision-free link name using backward path escalation.
 */
char *generate_collision_free_name(LinkHashSet *set, const char *anchor,
                                   char **segments, int num_segments);

/**
 * \brief Check if Content-Type string indicates HTML.
 */
int is_html_content_type(const char *ct);

/**
 * \brief Parse HTML content and populate LinkTable with unique links.
 */
void LinkTable_parse_html(LinkTable *linktbl, const char *url,
                          const char *html);

#endif
