#ifndef CONFIG_H
#define CONFIG_H

/**
 * \brief the maximum length of a path and a URL.
 * \details This corresponds the maximum path length under Ext4.
 */
#define MAX_PATH_LEN 4096

/**
 * \brief the maximum length of a filename.
 * \details This corresponds the filename length under Ext4.
 */
#define MAX_FILENAME_LEN 255

/**
 * \brief the default user agent string
 */
#define DEFAULT_USER_AGENT "HTTPDirFS-" VERSION

/**
 * \brief The default maximum number of network connections
 */
#define DEFAULT_NETWORK_MAX_CONNS 10

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

/**
 * \brief Maximum segment block count
 * \details This is set to 128*1024 blocks, which uses 128KB. By default,
 * this allows the user to store (128*1024)*(8*1024*1024) = 1TB of data
 */
#define DEFAULT_MAX_SEGBC (128 * 1024)

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

#endif
