#include "log.h"

#include <stdarg.h>
#include <stdio.h>

int log_init()
{
    char *env = getenv("HTTPDIRFS_DEBUG_LEVEL");
    if (env) {
        return atoi(env);
    }
    return DEFAULT_LOG_LEVEL;
}

void LOG_PRINTF(int level, const char *file, int line, const char *format, ...)
{
    fprintf(stderr, "(%s:%d): ", file, line);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}