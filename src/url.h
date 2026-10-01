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

#ifndef URL_H
#define URL_H

/**
 * \file url.h
 * \brief URL parsing, origin matching, and URI manipulation header
 */

#include <stddef.h>

/**
 * \brief Check if a URL is an external (absolute) http/https URL.
 * \return 1 if the URL starts with http:// or https://, 0 otherwise
 */
int is_external_url(const char *url);

/**
 * \brief Check if link_url has a different origin than page_url.
 * \details Compares scheme + host + port. Malformed URLs are treated as
 * cross-origin.
 * \return 1 if cross-origin or either URL is malformed, 0 if same origin
 */
int is_cross_origin(const char *page_url, const char *link_url);

/**
 * \brief Extract the filename component from an external URL.
 * \details For "http://example.com/path/file.iso" returns "file.iso".
 *          For "http://example.com/path/dir/" returns "dir".
 *          Query strings are stripped. Returns "" for root-only URLs.
 * \note The caller must free the returned string with FREE().
 */
char *external_url_to_filename(const char *url);

/**
 * \brief Extract the server root (scheme://host[:port]) from a URL.
 * \note The caller must free the returned string with FREE().
 */
char *get_server_root(const char *url);

/**
 * \brief Get pointer to the path component from the root of the server.
 * \details For "http://example.com/a/b", returns "/a/b".
 *          For "http://example.com/", returns "/".
 *          For "http://example.com", returns "".
 */
const char *get_url_path_from_server_root(const char *url);

/**
 * \brief Safely generate the cache path for a given URL, handling cross-origin
 * external links.
 * \note The caller must free the returned string with FREE().
 */
char *url_to_cache_path(const char *url);

/**
 * \brief Convert a link URL to be relative to the parent page URL.
 * \param page_url The URL of the parent directory page.
 * \param link_url The URL from the href attribute to convert in-place.
 */
void make_link_relative(const char *page_url, char *link_url);

/**
 * \brief Resolve an href attribute to a full canonical URL.
 */
int resolve_target_url(const char *page_url, const char *raw_href,
                       char *out_url, size_t out_size);

/**
 * \brief Tokenize URL path into slash-delimited segments.
 */
int extract_url_path_segments(const char *url, char ***segments_out,
                              int *num_segments_out);

/**
 * \brief Free URL path segments array.
 */
void free_url_path_segments(char **segments, int num_segments);

/**
 * \brief Check if target_url matches head_url, ignoring trailing slashes and
 * query strings if head_url has none.
 * \return 1 if matching, 0 otherwise
 */
int url_matches_head_link(const char *target_url, const char *head_url);

#endif
