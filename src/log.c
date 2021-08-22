#include "log.h"

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

int log_level_init()
{
    char *env = getenv("HTTPDIRFS_LOG_LEVEL");
    if (env) {
        return atoi(env);
    }
    return DEFAULT_LOG_LEVEL;
}

int log_verbosity_init()
{
    char *env = getenv("HTTPDIRFS_LOG_VERBOSITY");
    if (env) {
        return atoi(env);
    }
    return DEFAULT_LOG_VERBOSITY;
}

void log_printf(int type, const char *file, int line, const char *format, ...)
{
    if (type & CONFIG.log_level) {
        switch (type) {
            case notice:
                goto print_actual_message;
                break;
            case error:
                fprintf(stderr, "Error: ");
                break;
            case debug:
                fprintf(stderr, "Debug: ");
                break;
            default:
                fprintf(stderr, "Unknown (%x):", type);
                break;
        }
        switch (CONFIG.log_verbosity) {
            case LOG_SHOW_FILENAME:
                fprintf(stderr, "(%s): ", file);
                break;
            case LOG_SHOW_FILENAME_LINE_NUM:
                fprintf(stderr, "(%s:%d): ", file, line);
                break;
        }
        print_actual_message:
        /* A label can only be part of a statement, this is a statement. lol*/
        {}
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
    }
}