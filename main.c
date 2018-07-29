#include "network.h"
#include "fuse_local.h"

#include <getopt.h>
#include <string.h>

void add_arg(char ***fuse_argv_ptr, int *fuse_argc, char *opt_string);
static void print_help(char *program_name);

/**
 * \brief add an argument to an argv array
 * \details This is basically how you add a string to an array of string
 */
void add_arg(char ***fuse_argv_ptr, int *fuse_argc, char *opt_string)
{
    (*fuse_argc)++;
    *fuse_argv_ptr = realloc(*fuse_argv_ptr, *fuse_argc * sizeof(char *));
    char **fuse_argv = *fuse_argv_ptr;
    fuse_argv[*fuse_argc - 1] = opt_string;
}

int main(int argc, char **argv)
{
    char **fuse_argv = NULL;
    int fuse_argc = 0;

    /* Add the program's name to the fuse argument */
    add_arg(&fuse_argv, &fuse_argc, argv[0]);
    /* Automatically print help if not enough arguments are supplied */
    if (argc < 2) {
        add_arg(&fuse_argv, &fuse_argc, "--help");
        goto fuse_start;
    }

    char c;
    int opts_index = 0;
    const char *short_opts = "o:hVdfs";
    const struct option long_opts[] = {
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V'},
        {"debug", no_argument, NULL, 'd'},
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
                print_help(argv[0]);
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
            case '?':
                exit(EXIT_FAILURE);
        }
    };

    /* Add the last remaining argument, which is the mount point */
    add_arg(&fuse_argv, &fuse_argc, argv[argc-1]);

    /* The second last remaining argument is the URL */
    char *base_url = argv[argc-2];
    if (strncmp(base_url, "http://", 7) && strncmp(base_url, "https://", 8)) {
        fprintf(stderr, "Error: Please supply a valid URL.\n");
        print_help(argv[0]);
        exit(EXIT_FAILURE);
    } else {
        network_init(base_url);
    }

    fuse_start:
    fuse_local_init(fuse_argc, fuse_argv);

    return 0;
}

static void print_help(char *program_name)
{
    fprintf(stderr,
            "Usage: %s [options] URL mount_point\n", program_name);
}

