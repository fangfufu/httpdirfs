#include "network.h"
#include "fuse_local.h"

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#define ARG_LEN_MAX 64

void add_arg(char ***fuse_argv_ptr, int *fuse_argc, char *opt_string);
static void print_help(char *program_name, int long_help);
static void print_http_options();
static int
parse_arg_list(int argc, char **argv, char ***fuse_argv, int *fuse_argc);
void parse_config_file(char ***fuse_argv, int *fuse_argc);

int main(int argc, char **argv)
{
    char **fuse_argv = NULL;
    int fuse_argc = 0;

    /* Add the program's name to the fuse argument */
    add_arg(&fuse_argv, &fuse_argc, argv[0]);
    /* Automatically print help if not enough arguments are supplied */
    if (argc < 2) {
        print_help(argv[0], 0);
        fprintf(stderr, "For more information, run \"%s --help.\"\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /* initialise network configuration struct */
    network_config_init();

    /* Add the last remaining argument, which is the mountpoint */
    add_arg(&fuse_argv, &fuse_argc, argv[argc-1]);

    if (parse_arg_list(argc, argv, &fuse_argv, &fuse_argc)) {
        goto fuse_start;
    }

    /* parse the configuration file, if it exists */
    parse_config_file(&fuse_argv, &fuse_argc);

    /* The second last remaining argument is the URL */
    char *base_url = argv[argc-2];
    if (strncmp(base_url, "http://", 7) && strncmp(base_url, "https://", 8)) {
        fprintf(stderr, "Error: Please supply a valid URL.\n");
        print_help(argv[0], 0);
        exit(EXIT_FAILURE);
    } else {
        if(!network_init(base_url)) {
            fprintf(stderr, "Error: Network initialisation failed.\n");
            exit(EXIT_FAILURE);
        }
    }

    fuse_start:
    fuse_local_init(fuse_argc, fuse_argv);

    return 0;
}

void parse_config_file(char ***fuse_argv, int *fuse_argc)
{
    char *home = getenv("HOME");
    char *config_dir = "/.httpdirfs";
    char *main_config_name = "/config";
    int full_path_len = strnlen(home, 255) + strlen(config_dir) +
    strlen(main_config_name) + 1;
    char *full_path = calloc(full_path_len, sizeof(char *));
    strncat(full_path, home, strnlen(home, 255));
    strncat(full_path, config_dir, strlen(config_dir));
    strncat(full_path, main_config_name, strlen(main_config_name));

    int argc = 1;
    char **argv = NULL;
    /* The buffer has to be able to fit a URL */
    int buf_len = URL_LEN_MAX;
    char buf[buf_len];
    FILE *config = fopen(full_path, "r");
    if (config) {
        argv = malloc(1 * sizeof(char *));
        /*
         * getopt_long() expects the first parameter to be the name of the
         * program that was called.
         */
        argv[0] = "./httpdirfs";
        while (fgets(buf, buf_len, config)) {
            if (buf[0] == '-') {
                argc++;
                buf[strnlen(buf, buf_len) - 1] = '\0';
                char *space;
                space = strchr(buf, ' ');
                if (!space) {
                    argv = realloc(argv, argc * sizeof(char *));
                    argv[argc - 1] = strndup(buf, buf_len);
                } else {
                    argc++;
                    argv = realloc(argv, argc * sizeof(char *));
                    /* Only copy up to the space character*/
                    argv[argc - 2] = strndup(buf, space - buf);
                    /* Starts copying after the space */
                    argv[argc - 1] = strndup(space + 1, buf_len);
                }
            }
        }
        parse_arg_list(argc, argv, fuse_argv, fuse_argc);
        free(argv);
    }
}

static int
parse_arg_list(int argc, char **argv, char ***fuse_argv, int *fuse_argc)
{
    char c;
    int long_index = 0;
    const char *short_opts = "o:hVdfsp:u:P:";
    const struct option long_opts[] = {
        /* Note that 'L' is returned for long options */
        {"help", no_argument, NULL, 'h'},                   /* 0 */
        {"version", no_argument, NULL, 'V'},                /* 1 */
        {"debug", no_argument, NULL, 'd'},                  /* 2 */
        {"username", required_argument, NULL, 'u'},         /* 3 */
        {"password", required_argument, NULL, 'p'},         /* 4 */
        {"proxy", required_argument, NULL, 'P'},            /* 5 */
        {"proxy-username", required_argument, NULL, 'L'},   /* 6 */
        {"proxy-password", required_argument, NULL, 'L'},   /* 7 */
        {0, 0, 0, 0}
    };
    while ((c =
        getopt_long(argc, argv, short_opts, long_opts,
                    &long_index)) != -1) {
        switch (c) {
            case 'o':
                add_arg(fuse_argv, fuse_argc, "-o");
                add_arg(fuse_argv, fuse_argc, optarg);
                break;
            case 'h':
                print_help(argv[0], 1);
                add_arg(fuse_argv, fuse_argc, "-h");
                return 1;
            case 'V':
                add_arg(fuse_argv, fuse_argc, "-V");
                break;
            case 'd':
                add_arg(fuse_argv, fuse_argc, "-d");
                break;
            case 'f':
                add_arg(fuse_argv, fuse_argc, "-f");
                break;
            case 's':
                add_arg(fuse_argv, fuse_argc, "-s");
                break;
            case 'p':
                NETWORK_CONFIG.username = strndup(optarg, ARG_LEN_MAX);
                break;
            case 'u':
                NETWORK_CONFIG.password = strndup(optarg, ARG_LEN_MAX);
                break;
            case 'P':
                NETWORK_CONFIG.proxy = strndup(optarg, URL_LEN_MAX);
                break;
            case 'L':
                /* Long options */
                switch (long_index) {
                    case 6:
                        NETWORK_CONFIG.proxy_user = strndup(optarg,
                                                            ARG_LEN_MAX);
                        printf("proxy_user: %s\n", optarg);
                        break;
                    case 7:
                        NETWORK_CONFIG.proxy_pass = strndup(optarg,
                                                            ARG_LEN_MAX);
                        printf("proxy_pass: %s\n", optarg);
                        break;
                    default:
                        fprintf(stderr, "Error: Invalid option\n");
                        add_arg(fuse_argv, fuse_argc, "--help");
                        return 1;
                }
                break;
                    default:
                        fprintf(stderr, "Error: Invalid option\n");
                        add_arg(fuse_argv, fuse_argc, "--help");
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
    (*fuse_argc)++;
    *fuse_argv_ptr = realloc(*fuse_argv_ptr, *fuse_argc * sizeof(char *));
    char **fuse_argv = *fuse_argv_ptr;
    fuse_argv[*fuse_argc - 1] = strndup(opt_string, ARG_LEN_MAX);
}

static void print_help(char *program_name, int long_help)
{
    fprintf(stderr,
            "Usage: %s [options] URL mountpoint\n", program_name);
    if (long_help) {
        print_http_options();
    }
}

static void print_http_options()
{
    fprintf(stderr,
"HTTP options:\n\
    -u   --username        HTTP authentication username\n\
    -p   --password        HTTP authentication password\n\
    -P   --proxy           Proxy for libcurl, for more details refer to\n\
        https://curl.haxx.se/libcurl/c/CURLOPT_PROXY.html\n\
         --proxy-username      Username for the proxy\n\
         --proxy-password      Password for the proxy\n\
    \n\
libfuse options:\n");
}
