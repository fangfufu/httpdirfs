#include "config.h"

#include "log.h"
#include <stddef.h>

/**
 * \brief The default HTTP 429 (too many requests) wait time
 */
#define DEFAULT_HTTP_WAIT_SEC       5
/**
 * \brief Data file block size
 * \details We set it to 1024*1024*8 = 8MiB
 */
#define DEFAULT_DATA_BLKSZ         8*1024*1024

/**
 * \brief Maximum segment block count
 * \details This is set to 128*1024 blocks, which uses 128KB. By default,
 * this allows the user to store (128*1024)*(8*1024*1024) = 1TB of data
 */
#define DEFAULT_MAX_SEGBC           128*1024

ConfigStruct CONFIG;

/**
 * \note The opening curly bracket should be at line 39, so the code lines up
 * with the definition code in util.h.
 */
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

    CONFIG.insecure_tls = 0;

    CONFIG.refresh_timeout = DEFAULT_REFRESH_TIMEOUT;

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
}
