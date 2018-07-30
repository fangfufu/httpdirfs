#include "network.h"
#include "fuse_local.h"

#include <getopt.h>
#include <string.h>

#define ARG_LEN_MAX 64

void add_arg(char ***fuse_argv_ptr, int *fuse_argc, char *opt_string);
static void print_help(char *program_name, int long_help);
static void print_http_options();

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

    char c;
    int opts_index = 0;
    const char *short_opts = "o:hVdfsp:u:";
    const struct option long_opts[] = {
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V'},
        {"debug", no_argument, NULL, 'd'},
        {"username", required_argument, NULL, 'u'},
        {"password", required_argument, NULL, 'p'},
        {0, 0, 0, 0}
    };
    while ((c =
        getopt_long(argc, argv, short_opts, long_opts,
                    &opts_index)) != -1) {
        switch (c) {
            case 'o':
                add_arg(&fuse_argv, &fuse_argc, "-o");
                add_arg(&fuse_argv, &fuse_argc, optarg);
                break;
            case 'h':
                print_help(argv[0], 1);
                add_arg(&fuse_argv, &fuse_argc, "-h");
                goto fuse_start;
                break;
            case 'V':
                add_arg(&fuse_argv, &fuse_argc, "-V");
                break;
            case 'd':
                add_arg(&fuse_argv, &fuse_argc, "-d");
                break;
            case 'f':
                add_arg(&fuse_argv, &fuse_argc, "-f");
                break;
            case 's':
                add_arg(&fuse_argv, &fuse_argc, "-s");
                break;
            case 'p':
                NETWORK_CONFIG.username = strndup(optarg, ARG_LEN_MAX);
                break;
            case 'u':
                NETWORK_CONFIG.password = strndup(optarg, ARG_LEN_MAX);
                break;
            case '?':
                fprintf(stderr, "Error: Invalid option\n");
                add_arg(&fuse_argv, &fuse_argc, "--help");
                goto fuse_start;
        }
    };

    /* Add the last remaining argument, which is the mountpoint */
    add_arg(&fuse_argv, &fuse_argc, argv[argc-1]);

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
    -p   --password        HTTP authentication password\n\n\
libfuse options:\n");
}
