#ifndef MAIN_H
#define MAIN_H
extern char *config_path;

void parse_config_file(char ***argv, int *argc);
int parse_arg_list(int argc, char **argv, char ***fuse_argv, int *fuse_argc);

/**
 * \brief add an argument to an argv array
 * \details This is basically how you add a string to an array of string
 */
void add_arg(char ***fuse_argv_ptr, int *fuse_argc, char *opt_string);

void print_help(char *program_name, int long_help);

void print_version();

void print_long_help();

#endif