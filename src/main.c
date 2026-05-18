#include "fuse_local.h"
#include "link.h"
#include "log.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>

void add_arg(char ***fuse_argv_ptr, int *fuse_argc, char *opt_string);
static void print_help(char *program_name, int long_help);
static void print_long_help(void);
static int parse_arg_list(int argc, char **argv, char ***fuse_argv,
                          int *fuse_argc);
void parse_config_file(char ***argv, int *argc);

static char *config_path = NULL;

int main(int argc, char **argv)
{
    /*
     * Automatically print help if not enough arguments are supplied
     */
    if (argc < 2) {
        print_help(argv[0], 0);
        fprintf(stderr, "For more information, run \"%s --help.\"\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /*
     * These are passed into fuse initialiser
     */
    char **fuse_argv = NULL;
    int fuse_argc = 0;
    int res = 0;
    /*
     * These are the combined argument with the config file
     */
    char **all_argv = NULL;
    int all_argc = 0;

    /*--- Add the program's name to the combined argument list ---*/
    add_arg(&all_argv, &all_argc, argv[0]);
    /*--- FUSE expects the first initialisation to be the program's name ---*/
    add_arg(&fuse_argv, &fuse_argc, argv[0]);

    /*
     * initialise network configuration struct
     */
    Config_init();

    /*
     * initialise network subsystem
     */
    NetworkSystem_init();

    /*
     * Copy the command line argument list to the combined argument list
     */
    for (int i = 1; i < argc; i++) {
        add_arg(&all_argv, &all_argc, argv[i]);
        if (!strncmp(argv[i], "--config=", 9)) {
            if (argv[i][9] == '\0') {
                lprintf(fatal, "--config requires a path\n");
            }
            FREE(config_path);
            config_path = STRDUP(argv[i] + 9);
        } else if (!strcmp(argv[i], "--config")) {
            if (i + 1 >= argc || argv[i + 1][0] == '\0'
                || argv[i + 1][0] == '-') {
                lprintf(fatal, "--config requires a path\n");
            }
            FREE(config_path);
            config_path = STRDUP(argv[i + 1]);
            add_arg(&all_argv, &all_argc, argv[i + 1]);
            i++;
        }
    }

    /*
     * parse the config file, if it exists, store it in all_argv and
     * all_argc
     */
    parse_config_file(&all_argv, &all_argc);

    /*
     * parse the combined argument list
     */
    optind = 1;
    if (parse_arg_list(all_argc, all_argv, &fuse_argv, &fuse_argc)) {
        /*
         * The user basically didn't supply enough arguments, if we reach here
         * The point is to print some error messages
         */
        goto fuse_start;
    }

    if (optind + 2 != all_argc) {
        fprintf(
            stderr,
            "Error: You must provide exactly one URL and one mountpoint.\n");
        print_help(argv[0], 0);
        exit(EXIT_FAILURE);
    }

    /*--- Check if FUSE is available ---*/
    if (access("/dev/fuse", F_OK) == -1) {
        fprintf(stderr, "Error: FUSE kernel module is not loaded.\n");
        exit(EXIT_FAILURE);
    }

    /*--- Add the last remaining argument, which is the mountpoint ---*/
    char *abs_mountpoint = REALPATH(all_argv[optind + 1], NULL);
    if (!abs_mountpoint) {
        fprintf(stderr, "Error: Invalid mountpoint %s: %s\n",
                all_argv[optind + 1], strerror(errno));
        print_help(argv[0], 0);
        exit(EXIT_FAILURE);
    }

    struct stat st;
    if (stat(abs_mountpoint, &st) == -1) {
        fprintf(stderr, "Error: Could not stat mountpoint %s: %s\n",
                abs_mountpoint, strerror(errno));
        FREE(abs_mountpoint);
        exit(EXIT_FAILURE);
    }

    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: Mountpoint %s is not a directory.\n",
                abs_mountpoint);
        FREE(abs_mountpoint);
        exit(EXIT_FAILURE);
    }

    add_arg(&fuse_argv, &fuse_argc, abs_mountpoint);
    FREE(abs_mountpoint);

    /*
     * The second last remaining argument is the URL
     */
    char *base_url = all_argv[optind];

    if (strncmp(base_url, "http://", 7) != 0
        && strncmp(base_url, "https://", 8) != 0) {
        fprintf(stderr, "Error: Please supply a valid URL.\n");
        print_help(argv[0], 0);
        exit(EXIT_FAILURE);
    } else {
        if (CONFIG.sonic_username && CONFIG.sonic_password) {
            CONFIG.mode = SONIC;
        } else if (CONFIG.sonic_username || CONFIG.sonic_password) {
            fprintf(stderr,
                    "Error: You have to supply both username and password to \
activate Sonic mode.\n");
            exit(EXIT_FAILURE);
        }
        if (!LinkSystem_init(base_url)) {
            fprintf(stderr, "Network initialisation failed.\n");
            exit(EXIT_FAILURE);
        }
    }

fuse_start:
    res = fuse_local_init(fuse_argc, fuse_argv);

    for (int i = 0; i < all_argc; i++) {
        FREE(all_argv[i]);
    }
    FREE(all_argv);

    for (int i = 0; i < fuse_argc; i++) {
        FREE(fuse_argv[i]);
    }
    FREE(fuse_argv);

    FREE(config_path);

    return res;
}

static char *get_XDG_CONFIG_HOME(void)
{
    const char *default_config_subdir = "/.config";
    char *config_dir = NULL;

    const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
    if (xdg_config_home) {
        config_dir = STRNDUP(xdg_config_home, PATH_MAX);
    } else {
        const char *user_home = getenv("HOME");
        if (user_home) {
            config_dir = path_append(user_home, default_config_subdir);
        } else {
            lprintf(warning, "$HOME is unset\n");
            const char *cur_dir = REALPATH("./", NULL);
            if (cur_dir) {
                config_dir = path_append(cur_dir, default_config_subdir);
            } else {
                lprintf(warning, "Could not get config directory\n");
            }
        }
    }
    return config_dir;
}

void parse_config_file(char ***argv, int *argc)
{
    char *full_path;
    if (!config_path) {
        char *xdg_config_home = get_XDG_CONFIG_HOME();
        full_path = path_append(xdg_config_home, "/httpdirfs/config");
        FREE(xdg_config_home);
    } else {
        full_path = config_path;
    }

    char *buf = NULL;
    size_t buf_len = 0;
    FILE *config = fopen(full_path, "r");
    if (!config) {
        if (config_path) {
            lprintf(fatal, "Could not open config file %s: %s\n", full_path,
                    strerror(errno));
        }
    } else {
        while (getline(&buf, &buf_len, config) != -1) {
            // Remove trailing whitespace
            size_t len = strlen(buf);
            while (len > 0 && isspace((unsigned char)buf[len - 1])) {
                buf[--len] = '\0';
            }

            char *line = buf;
            while (*line && isspace((unsigned char)*line)) {
                line++;
            }

            if (line[0] == '-') {
                char *space = strpbrk(line, " \t");
                if (!space) {
                    add_arg(argv, argc, line);
                } else {
                    *space = '\0';
                    add_arg(argv, argc, line);
                    /*
                     * Starts copying after the space, skipping leading
                     * whitespace
                     */
                    char *value = space + 1;
                    while (*value && isspace((unsigned char)*value)) {
                        value++;
                    }
                    if (*value != '\0') {
                        add_arg(argv, argc, value);
                    }
                }
            }
        }
        FREE(buf);
        fclose(config);
    }
    if (full_path != config_path) {
        FREE(full_path);
    }
}

static int parse_arg_list(int argc, char **argv, char ***fuse_argv,
                          int *fuse_argc)
{
    int c;
    int long_index = 0;
    const char *short_opts = "o:hVdfsp:u:P:";
    const struct option long_opts[]
        = {                                                   /*
                                                               * Note that 'L' is returned for long options
                                                               */
           {"help", no_argument, NULL, 'h'},                  /* 0 */
           {"version", no_argument, NULL, 'V'},               /* 1 */
           {"debug", no_argument, NULL, 'd'},                 /* 2 */
           {"username", required_argument, NULL, 'u'},        /* 3 */
           {"password", required_argument, NULL, 'p'},        /* 4 */
           {"proxy", required_argument, NULL, 'P'},           /* 5 */
           {"proxy-username", required_argument, NULL, 'L'},  /* 6 */
           {"proxy-password", required_argument, NULL, 'L'},  /* 7 */
           {"cache", no_argument, NULL, 'L'},                 /* 8 */
           {"dl-seg-size", required_argument, NULL, 'L'},     /* 9 */
           {"max-conns", required_argument, NULL, 'L'},       /* 10 */
           {"user-agent", required_argument, NULL, 'L'},      /* 11 */
           {"retry-wait", required_argument, NULL, 'L'},      /* 12 */
           {"cache-location", required_argument, NULL, 'L'},  /* 13 */
           {"sonic-username", required_argument, NULL, 'L'},  /* 14 */
           {"sonic-password", required_argument, NULL, 'L'},  /* 15 */
           {"sonic-id3", no_argument, NULL, 'L'},             /* 16 */
           {"no-range-check", no_argument, NULL, 'L'},        /* 17 */
           {"sonic-insecure", no_argument, NULL, 'L'},        /* 18 */
           {"insecure-tls", no_argument, NULL, 'L'},          /* 19 */
           {"config", required_argument, NULL, 'L'},          /* 20 */
           {"single-file-mode", no_argument, NULL, 'L'},      /* 21 */
           {"cacert", required_argument, NULL, 'L'},          /* 22 */
           {"proxy-cacert", required_argument, NULL, 'L'},    /* 23 */
           {"refresh-timeout", required_argument, NULL, 'L'}, /* 24 */
           {"http-header", required_argument, NULL, 'L'},     /* 25 */
           {"cache-clear", no_argument, NULL, 'L'},           /* 26 */
           {"zero-len-is-dir", no_argument, NULL, 'L'},       /* 27 */
           {"invalid-refresh", no_argument, NULL, 'L'},       /* 28 */
           {0, 0, 0, 0}};
    while ((c = getopt_long(argc, argv, short_opts, long_opts, &long_index))
           != -1) {
        switch (c) {
        case 'o':
            add_arg(fuse_argv, fuse_argc, "-o");
            add_arg(fuse_argv, fuse_argc, optarg);
            break;
        case 'h':
            print_help(argv[0], 1);
            add_arg(fuse_argv, fuse_argc, "-ho");
            /*
             * skip everything else to print the help
             */
            return 1;
        case 'V':
            print_version();
            add_arg(fuse_argv, fuse_argc, "-V");
            return 1;
        case 'd':
            add_arg(fuse_argv, fuse_argc, "-d");
            CONFIG.log_type |= debug;
            break;
        case 'f':
            add_arg(fuse_argv, fuse_argc, "-f");
            break;
        case 's':
            add_arg(fuse_argv, fuse_argc, "-s");
            break;
        case 'u':
            CONFIG.http_username = STRDUP(optarg);
            break;
        case 'p':
            CONFIG.http_password = STRDUP(optarg);
            break;
        case 'P':
            CONFIG.proxy = STRDUP(optarg);
            break;
        case 'L':
            /*
             * Long options
             */
            switch (long_index) {
            case 6:
                CONFIG.proxy_username = STRDUP(optarg);
                break;
            case 7:
                CONFIG.proxy_password = STRDUP(optarg);
                break;
            case 8:
                CONFIG.cache_enabled = 1;
                break;
            case 9:
                CONFIG.data_blksz = (int)strtol(optarg, NULL, 10) * 1024 * 1024;
                break;
            case 10:
                CONFIG.max_conns = (int)strtol(optarg, NULL, 10);
                break;
            case 11:
                CONFIG.user_agent = STRDUP(optarg);
                break;
            case 12:
                CONFIG.http_wait_sec = (int)strtol(optarg, NULL, 10);
                break;
            case 13:
                CONFIG.cache_dir = STRDUP(optarg);
                break;
            case 14:
                CONFIG.sonic_username = STRDUP(optarg);
                break;
            case 15:
                CONFIG.sonic_password = STRDUP(optarg);
                break;
            case 16:
                CONFIG.sonic_id3 = 1;
                break;
            case 17:
                CONFIG.no_range_check = 1;
                break;
            case 18:
                CONFIG.sonic_insecure = 1;
                break;
            case 19:
                CONFIG.insecure_tls = 1;
                break;
            case 20:
                /*
                 * This is for --config, we don't need to do anything
                 */
                break;
            case 21:
                CONFIG.mode = SINGLE;
                break;
            case 22:
                CONFIG.cafile = STRDUP(optarg);
                break;
            case 23:
                CONFIG.proxy_cafile = STRDUP(optarg);
                break;
            case 24:
                CONFIG.refresh_timeout = (int)strtol(optarg, NULL, 10);
                break;
            case 25:
                CONFIG.http_headers
                    = curl_slist_append(CONFIG.http_headers, optarg);
                break;
            case 26:
                CacheSystem_clear();
                break;
            case 27:
                CONFIG.zero_len_is_dir = 1;
                break;
            case 28:
                CONFIG.invalid_refresh = 1;
                break;
            default:
                fprintf(stderr, "see httpdirfs -h for usage\n");
                return 1;
            }
            break;
        default:
            fprintf(stderr, "see httpdirfs -h for usage\n");
            return 1;
        }
    };
    return 0;
}

/**
 * \brief add an argument to an argv array
 * \details This is basically how you add a string to an array of string
 */
void add_arg(char ***fuse_argv_ptr, int *fuse_argc, char *opt_string)
{
    char **new_argv = (char **)REALLOC(
        (void *)*fuse_argv_ptr, ((size_t)*fuse_argc + 1) * sizeof(char *));
    if (!new_argv) {
        lprintf(fatal, "Out of memory in add_arg!\n");
    }
    *fuse_argv_ptr = new_argv;
    char **fuse_argv = *fuse_argv_ptr;
    fuse_argv[*fuse_argc] = STRDUP(opt_string);
    (*fuse_argc)++;
}

static void print_help(char *program_name, int long_help)
{
    /* FUSE prints its help to stderr */
    fprintf(stderr, "usage: %s [options] URL mountpoint\n", program_name);
    if (long_help) {
        print_long_help();
    }
}

static void print_long_help(void)
{
    /* FUSE prints its help to stderr */
    fprintf(stderr, "\n\
general options:\n\
        --config            Specify a configuration file \n\
    -o opt,[opt...]         Mount options\n\
    -h  --help              Print help\n\
    -V  --version           Print version\n\
    -f                      Foreground operation\n\
    -s                      Disable multi-threaded operation\n\
    -d  --debug             Enable debug output (implies -f)\n\
\n\
HTTPDirFS options:\n\
    -u  --username          HTTP authentication username\n\
    -p  --password          HTTP authentication password\n\
    -P  --proxy             Proxy for libcurl, for more details refer to\n\
                            https://curl.haxx.se/libcurl/c/CURLOPT_PROXY.html\n\
        --proxy-username    Username for the proxy\n\
        --proxy-password    Password for the proxy\n\
        --proxy-cacert      Certificate authority for the proxy\n\
        --cache             Enable cache (default: off)\n\
        --cache-location    Set a custom cache location\n\
                            (default: \"${XDG_CACHE_HOME}/httpdirfs\")\n\
        --cache-clear       Delete the cache directory or the custom location\n\
                            specified with `--cache-location`, if the option is\n\
                            seen first. Then exit in either case.\n\
        --cacert            Certificate authority for the server\n\
        --dl-seg-size       Set cache download segment size, in MB (default: " XSTR(
                        DEFAULT_DATA_BLKSZ_MB) ")\n\
                            Note: this setting is ignored if previously\n\
                            cached data is found for the requested file.\n\
        --http-header       Set one or more HTTP headers\n\
        --max-conns         Set maximum number of network connections that\n\
                            libcurl is allowed to make. (default: " XSTR(DEFAULT_NETWORK_MAX_CONNS) ")\n\
        --refresh-timeout   The directories are refreshed after the specified\n\
                            time, in seconds (default: " XSTR(DEFAULT_REFRESH_TIMEOUT) ")\n\
        --retry-wait        Set delay in seconds before retrying an HTTP request\n\
                            after encountering an error. (default: " XSTR(DEFAULT_HTTP_WAIT_SEC) ")\n\
        --invalid-refresh   Try refreshing invalid links when reading a directory.\n\
        --user-agent        Set user agent string (default: \"" DEFAULT_USER_AGENT "\")\n\
        --no-range-check    Disable the built-in check for the server's support\n\
                            for HTTP range requests\n\
        --zero-len-is-dir   If a file has a zero length, treat it as a directory\n\
        --insecure-tls      Disable licurl TLS certificate verification by\n\
                            setting CURLOPT_SSL_VERIFYHOST to 0\n\
        --single-file-mode  Single file mode - rather than mounting a whole\n\
                            directory, present a single file inside a virtual\n\
                            directory.\n\
\n\
    For mounting a Airsonic / Subsonic server:\n\
        --sonic-username    The username for your Airsonic / Subsonic server\n\
        --sonic-password    The password for your Airsonic / Subsonic server\n\
        --sonic-id3         Enable ID3 mode - this present the server content in\n\
                            Artist/Album/Song layout \n\
        --sonic-insecure    Authenticate against your Airsonic / Subsonic server\n\
                            using the insecure username / hex encoded password\n\
                            scheme\n\
\n");
}
