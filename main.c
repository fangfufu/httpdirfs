#include "network.h"
#include "fuse_local.h"

#include <getopt.h>
#include <string.h>

void add_arg(char ***new_argv_ptr, int *new_argc, char *opt_string);
static void print_help();

/**
 * \brief add an argument to an argv array
 * \details This is basically how you add a string to an array of string
 */
void add_arg(char ***new_argv_ptr, int *new_argc, char *opt_string)
{
    (*new_argc)++;
    *new_argv_ptr = realloc(*new_argv_ptr, *new_argc * sizeof(char *));
    char **new_argv = *new_argv_ptr;
    new_argv[*new_argc - 1] = opt_string;
}

int main(int argc, char **argv)
{

    char **new_argv = NULL;
    int new_argc = 0;

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
                add_arg(&new_argv, &new_argc, " -o ");
                add_arg(&new_argv, &new_argc, optarg);
                break;
            case 'h':
                print_help();
                add_arg(&new_argv, &new_argc, " -h ");
                goto fuse_start;
                break;
            case 'V':
                add_arg(&new_argv, &new_argc, " -V ");
                break;
            case 'd':
                add_arg(&new_argv, &new_argc, " -d ");
                break;
            case 'f':
                add_arg(&new_argv, &new_argc, " -f ");
                break;
            case 's':
                add_arg(&new_argv, &new_argc, " -s");
                break;
            case '?':
                exit(EXIT_FAILURE);
        }
    };

    /* The second last remaining argument is the URL */
    char *base_url = argv[argc-2];
    if (strncmp(base_url, "http://", 7) && strncmp(base_url, "https://", 8)) {
        fprintf(stderr, "Please supply a valid URL.\n");
        print_help();
        exit(EXIT_FAILURE);
    }
    network_init(base_url);

    /* The last remaining argument is the mount point */
    add_arg(&new_argv, &new_argc, argv[argc-1]);

    fuse_start:
    fuse_local_init(new_argc, new_argv);

    return 0;
}

static void print_help()
{
    fprintf(stderr,
            "usage: mount-http-dir [options] URL mount_point\n");
}

