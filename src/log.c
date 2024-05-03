#include "log.h"

#include "config.h"
#include "util.h"

#include <curl/curl.h>

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

void
log_printf(LogType type, const char *file, const char *func, int line,
           const char *format, ...)
{
    if (type & CONFIG.log_type) {
        switch (type) {
        case fatal:
            fprintf(stderr, "Fatal:");
            break;
        case error:
            fprintf(stderr, "Error:");
            break;
        case warning:
            fprintf(stderr, "Warning:");
            break;
        case info:
            goto print_actual_message;
        default:
            fprintf(stderr, "Debug");
            if (type != debug) {
                fprintf(stderr, "(%x)", type);
            }
            fprintf(stderr, ":");
            break;
        }

        fprintf(stderr, "%s:%d:", file, line);

print_actual_message: {
        }
        fprintf(stderr, "%s: ", func);
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);

        if (type == fatal) {
            exit_failure();
        }
    }
}

void print_version()
{
    /* FUSE prints its help to stderr */
    fprintf(stderr, "HTTPDirFS version " VERSION "\n");
    /*
     * --------- Print off SSL engine version ---------
     */
    curl_version_info_data *data = curl_version_info(CURLVERSION_NOW);
    fprintf(stderr, "libcurl SSL engine: %s\n", data->ssl_version);
}
