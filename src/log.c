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

void log_printf(LogType type, const char *file, int line, const char *format, ...)
{
    FILE *out = stderr;
    if (type & CONFIG.log_level) {
        switch (type) {
            case fatal:
                fprintf(out, "Fatal: ");
                break;
            case error:
                fprintf(out, "Error: ");
                break;
            case warning:
                fprintf(out, "Warning: ");
                break;
            case info:
                out = stdout;
                goto print_actual_message;
                break;
            case debug:
                fprintf(out, "Debug: ");
                break;
            default:
                fprintf(out, "Unknown (%x):", type);
                break;
        }

        fprintf(out, "(%s:%d): ", file, line);

        print_actual_message:
        /* A label can only be part of a statement, this is a statement. lol*/
        {}
        va_list args;
        va_start(args, format);
        vfprintf(out, format, args);
        va_end(args);
    }
}