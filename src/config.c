#include "config.h"

#include "log.h"
#include "util.h"
#include <stddef.h>
#include <stdlib.h>


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

    CONFIG.max_conns = DEFAULT_NETWORK_MAX_CONNS;

    CONFIG.user_agent = DEFAULT_USER_AGENT;

    CONFIG.http_wait_sec = DEFAULT_HTTP_WAIT_SEC;

    CONFIG.http_headers = NULL;

    CONFIG.no_range_check = 0;

    CONFIG.zero_len_is_dir = 0;

    CONFIG.insecure_tls = 0;

    CONFIG.cafile = NULL;

    CONFIG.capath = NULL;

    CONFIG.refresh_timeout = DEFAULT_REFRESH_TIMEOUT;

    CONFIG.invalid_refresh = 0;

    /*--------------- Cache related ---------------*/
    CONFIG.cache_enabled = 0;

    CONFIG.cache_dir = NULL;

    CONFIG.data_blksz = DEFAULT_DATA_BLKSZ;

    /*-------------- Sonic related -------------*/
    CONFIG.sonic_username = NULL;

    CONFIG.sonic_password = NULL;

    CONFIG.sonic_id3 = 0;

    CONFIG.sonic_insecure = 0;
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
        && strcmp(CONFIG.user_agent, DEFAULT_USER_AGENT) != 0) {
        FREE(CONFIG.user_agent);
    }
    FREE(CONFIG.cafile);
    FREE(CONFIG.capath);
    FREE(CONFIG.cache_dir);
    FREE(CONFIG.sonic_username);
    FREE(CONFIG.sonic_password);
}
