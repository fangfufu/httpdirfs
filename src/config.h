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

#ifndef CONFIG_H
#define CONFIG_H
/**
 * \file config.h
 * \brief Configuration options and defaults header
 */

#include <limits.h>

/**
 * \brief the default user agent string
 */
#define DEFAULT_USER_AGENT "HTTPDirFS-" VERSION

/**
 * \brief The default maximum number of network connections
 */
#define DEFAULT_NETWORK_MAX_CONNS 6

/**
 * \brief The default refresh_timeout
 */
#define DEFAULT_REFRESH_TIMEOUT 3600

/**
 * \brief The default HTTP 429 (too many requests) wait time
 */
#define DEFAULT_HTTP_WAIT_SEC 5

/**
 * \brief Data file block size in MB
 */
#define DEFAULT_DATA_BLKSZ_MB 8

/**
 * \brief Data file block size
 * \details We set it to 1024*1024*8 = 8MiB
 */
#define DEFAULT_DATA_BLKSZ (DEFAULT_DATA_BLKSZ_MB * 1024 * 1024)

#define STR(x) #x
#define XSTR(x) STR(x)

/**
 * \brief Operation modes
 */
typedef enum {
    NORMAL = 1,
    SONIC = 2,
    SINGLE = 3,
} OperationMode;

typedef struct {
    /** \brief Operation Mode */
    OperationMode mode;
    /** \brief Current log level */
    int log_type;
    /*---------------- Network related --------------*/
    /** \brief HTTP username */
    char *http_username;
    /** \brief HTTP password */
    char *http_password;
    /** \brief HTTP proxy URL */
    char *proxy;
    /** \brief HTTP proxy username */
    char *proxy_username;
    /** \brief HTTP proxy password */
    char *proxy_password;
    /** \brief HTTP proxy certificate file */
    char *proxy_cafile;
    /** \brief HTTP proxy certificate directory */
    char *proxy_capath;
    /** \brief HTTP maximum connection count */
    long max_conns;
    /** \brief HTTP user agent*/
    char *user_agent;
    /** \brief The waiting time after getting HTTP 429 (too many requests) */
    int http_wait_sec;
    /** \brief Set HTTP headers */
    struct curl_slist *http_headers;
    /** \brief Disable check for the server's support of HTTP range request */
    int no_range_check;
    /** \brief Treat zero length file as directory */
    int zero_len_is_dir;
    /** \brief Disable TLS certificate verification */
    int insecure_tls;
    /** \brief Server certificate file */
    char *cafile;
    /** \brief Server certificate directory */
    char *capath;
    /** \brief Refresh directory listing after refresh_timeout seconds */
    int refresh_timeout;
    /** \brief Try refreshing invalid links when reading a directory */
    int invalid_refresh;
    /*--------------- Cache related ---------------*/
    /** \brief Whether cache mode is enabled */
    int cache_enabled;
    /** \brief The cache location*/
    char *cache_dir;
    /** \brief The size of each download segment for cache mode */
    int data_blksz;
    /** \brief The maximum segment count for a single cache file */
    int max_segbc;
    /*-------------- Sonic related -------------*/
    /** \brief The Sonic server username */
    char *sonic_username;
    /** \brief The Sonic server password */
    char *sonic_password;
    /** \brief Whether we are using sonic mode ID3 extension */
    int sonic_id3;
    /** \brief Whether we use the legacy sonic authentication mode */
    int sonic_insecure;
} ConfigStruct;

/**
 * \brief The Configuration data structure
 */
extern ConfigStruct CONFIG;

/**
 * \brief Free any heap-allocated configuration options
 */
void Config_cleanup(void);

#endif
