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

#ifndef NETWORK_H
#define NETWORK_H

/**
 * \file network.h
 * \brief Network transfer and cURL wrapper header
 */

#include <curl/curl.h>

/** \brief HTTP response codes */
typedef enum {
    HTTP_OK = 200,
    HTTP_PARTIAL_CONTENT = 206,
    HTTP_RANGE_NOT_SATISFIABLE = 416,
    HTTP_TOO_MANY_REQUESTS = 429,
    HTTP_CLOUDFLARE_UNKNOWN_ERROR = 520,
    HTTP_CLOUDFLARE_TIMEOUT = 524
} HTTPResponseCode;

/** \brief curl shared interface */
extern CURLSH *CURL_SHARE;

/** \brief perform one transfer cycle */
int curl_multi_perform_once(void);

/** \brief initialise the network module */
void NetworkSystem_init(void);

/** \brief blocking file transfer */
void transfer_blocking(CURL *curl);

/** \brief non blocking file transfer */
void transfer_nonblocking(CURL *curl);

/**
 * \brief check if a HTTP response code corresponds to a temporary failure
 */
int HTTP_temp_failure(HTTPResponseCode http_resp);

#endif
