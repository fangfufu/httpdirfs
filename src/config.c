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

/**
 * \file config.c
 * \brief Configuration options parsing implementation
 */

#include "config.h"

#include "log.h"
#include "util.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>


ConfigStruct CONFIG;

void Config_init(void)
{
    CONFIG.mode = NORMAL;

    CONFIG.log_type = log_level_init();

    /*---------------- Network related --------------*/
    CONFIG.http_username = NULL;

    CONFIG.http_password = NULL;

    CONFIG.proxy = NULL;

    CONFIG.proxy_username = NULL;

    CONFIG.proxy_password = NULL;

    CONFIG.proxy_cafile = NULL;

    CONFIG.proxy_capath = NULL;

    CONFIG.max_conns = 6;

    CONFIG.user_agent = "HTTPDirFS-" VERSION;

    CONFIG.http_wait_sec = 5;

    CONFIG.http_headers = NULL;

    CONFIG.no_range_check = 0;

    CONFIG.zero_len_is_dir = 0;

    CONFIG.insecure_tls = 0;

    CONFIG.cafile = NULL;

    CONFIG.capath = NULL;

    CONFIG.refresh_timeout = 3600;

    CONFIG.invalid_refresh = 0;

    CONFIG.allow_external_origin = 0;

    CONFIG.ignore_anchors = 0;

    /*--------------- Cache related ---------------*/
    CONFIG.cache_enabled = 0;

    CONFIG.cache_dir = NULL;

    CONFIG.data_blksz = 8 * 1024 * 1024;

    CONFIG.cache_min_size = -1;
    CONFIG.cache_max_size = -1;

    CONFIG.cache_clear = 0;
    CONFIG.cache_clear_host = NULL;

    /*-------------- Sonic related -------------*/
    CONFIG.sonic_username = NULL;

    CONFIG.sonic_password = NULL;

    CONFIG.sonic_id3 = 0;

    CONFIG.sonic_insecure = 0;

    /*--------- Directory promotion & traversal related --------*/
    CONFIG.website_mode = 0;
    CONFIG.max_html_size = (off_t)2 * 1024 * 1024;
    CONFIG.progressive_dir_preload = 0;

    atexit(mem_cleanup);
}

void Config_cleanup(void)
{
    FREE(CONFIG.http_username);
    FREE(CONFIG.http_password);
    FREE(CONFIG.proxy);
    FREE(CONFIG.proxy_username);
    FREE(CONFIG.proxy_password);
    FREE(CONFIG.proxy_cafile);
    FREE(CONFIG.proxy_capath);
    if (CONFIG.user_agent
        && strcmp(CONFIG.user_agent, "HTTPDirFS-" VERSION) != 0) {
        FREE(CONFIG.user_agent);
    }
    FREE(CONFIG.cafile);
    FREE(CONFIG.capath);
    FREE(CONFIG.cache_dir);
    FREE(CONFIG.cache_clear_host);
    FREE(CONFIG.sonic_username);
    FREE(CONFIG.sonic_password);
}
