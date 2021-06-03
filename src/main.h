#ifndef MAIN_H
#define MAIN_H

/**
 * \brief Configuration path
 */
extern char *config_path;

/**
 * \brief Parse the configuration file
 */
void parse_config_file(char ***argv, int *argc);

/**
 * \brief Parse argument list
 */
int parse_arg_list(int argc, char **argv, char ***fuse_argv, int *fuse_argc);

/**
 * \brief Add an argument to an argv array
 * \details This is basically how you add a string to an array of string
 */
void add_arg(char ***fuse_argv_ptr, int *fuse_argc, char *opt_string);

/**
 * \brief Print short help information
 */
void print_help(char *program_name, int long_help);

/**
 * \brief Print version number
 */
void print_version();

/**
 * \brief Print long help information
 */
void print_long_help();

/**
 * \brief Print Sonic server related help information
 */
void print_sonic_help();
#endif