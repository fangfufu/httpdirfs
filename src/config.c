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

    CONFIG.max_conns = DEFAULT_NETWORK_MAX_CONNS;

    CONFIG.user_agent = DEFAULT_USER_AGENT;

    CONFIG.http_wait_sec = DEFAULT_HTTP_WAIT_SEC;

    CONFIG.http_headers = NULL;

    CONFIG.no_range_check = 0;

    CONFIG.zero_len_is_dir = 0;

    CONFIG.insecure_tls = 0;

    CONFIG.refresh_timeout = DEFAULT_REFRESH_TIMEOUT;

    CONFIG.invalid_refresh = 0;

    /*--------------- Cache related ---------------*/
    CONFIG.cache_enabled = 0;

    CONFIG.cache_dir = NULL;

    CONFIG.data_blksz = DEFAULT_DATA_BLKSZ;

    CONFIG.max_segbc = DEFAULT_MAX_SEGBC;

    /*-------------- Sonic related -------------*/
    CONFIG.sonic_username = NULL;

    CONFIG.sonic_password = NULL;

    CONFIG.sonic_id3 = 0;

    CONFIG.sonic_insecure = 0;
    atexit(mem_cleanup);
}
