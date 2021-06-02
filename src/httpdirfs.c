
#include "main.h"

#include "util.h"
#include "fuse_local.h"
#include "network.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    /* Automatically print help if not enough arguments are supplied */
    if (argc < 2) {
        print_help(argv[0], 0);
        fprintf(stderr, "For more information, run \"%s --help.\"\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /* These are passed into fuse initialiser */
    char **fuse_argv = NULL;
    int fuse_argc = 0;
    /* These are the combined argument with the config file */
    char **all_argv = NULL;
    int all_argc = 0;

    /*--- Add the program's name to the combined argument list ---*/
    add_arg(&all_argv, &all_argc, argv[0]);
    /*--- FUSE expects the first initialisation to be the program's name ---*/
    add_arg(&fuse_argv, &fuse_argc, argv[0]);

    /* initialise network configuration struct */
    Config_init();

    /* initialise network subsystem */
    NetworkSystem_init();

    /* Copy the command line argument list to the combined argument list */
    for (int i = 1; i < argc; i++) {
        add_arg(&all_argv, &all_argc, argv[i]);
        if (!strcmp(argv[i], "--config")) {
            config_path = strdup(argv[i+1]);
        }
    }

    /* parse the config file, if it exists, store it in all_argv and all_argc */
    parse_config_file(&all_argv, &all_argc);

    /* parse the combined argument list */
    if (parse_arg_list(all_argc, all_argv, &fuse_argv, &fuse_argc)) {
        /*
         * The user basically didn't supply enough arguments, if we reach here
         * The point is to print some error messages
         */
        goto fuse_start;
    }

    /*--- Add the last remaining argument, which is the mountpoint ---*/
    add_arg(&fuse_argv, &fuse_argc, argv[argc-1]);

    /* The second last remaining argument is the URL */
    char *base_url = argv[argc-2];
    if (strncmp(base_url, "http://", 7) && strncmp(base_url, "https://", 8)) {
        fprintf(stderr, "Error: Please supply a valid URL.\n");
        print_help(argv[0], 0);
        exit(EXIT_FAILURE);
    } else {
        if (CONFIG.sonic_username && CONFIG.sonic_password) {
            CONFIG.sonic_mode = 1;
        } else if (CONFIG.sonic_username || CONFIG.sonic_password) {
            fprintf(stderr,
                    "Error: You have to supply both username and password to \
activate Sonic mode.\n");
            exit(EXIT_FAILURE);
        }
        if(!LinkSystem_init(base_url)) {
            fprintf(stderr, "Error: Network initialisation failed.\n");
            exit(EXIT_FAILURE);
        }
    }

    fuse_start:
    fuse_local_init(fuse_argc, fuse_argv);

    return 0;
}
