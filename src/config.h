#ifndef CONFIG_H
#define CONFIG_H

/**
 * \brief the maximum length of a path and a URL.
 * \details This corresponds the maximum path length under Ext4.
 */
#define MAX_PATH_LEN        4096

/**
 * \brief the maximum length of a filename.
 * \details This corresponds the filename length under Ext4.
 */
#define MAX_FILENAME_LEN    255

/**
 * \brief the default user agent string
 */
#define DEFAULT_USER_AGENT "HTTPDirFS-" VERSION

/**
 * \brief The default maximum number of network connections
 */
#define DEFAULT_NETWORK_MAX_CONNS   10

/**
 * \brief Operation modes
 */
typedef enum {
    NORMAL = 1,
    SONIC = 2,
    SINGLE_FILE = 3,
} OperationMode;

/**
 * \brief configuration data structure
 * \note The opening curly bracket should be at line 39, so the code belong
 * lines up with the initialisation code in util.c
 */
typedef struct {
    /** \brief Operation Mode */
    OperationMode mode;
    /** \brief Current log level */
    int log_level;
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
    /** \brief HTTP maximum connection count */
    long max_conns;
    /** \brief HTTP user agent*/
    char *user_agent;
    /** \brief The waiting time after getting HTTP 429 (too many requests) */
    int http_wait_sec;
    /** \brief Disable check for the server's support of HTTP range request */
    int no_range_check;
    /** \brief Disable TLS certificate verification */
    int insecure_tls;
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

#endif