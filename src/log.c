/*
 * HTTPDirFS - HTTP Directory Filesystem
 *
 * Copyright (C) 2020-2026 Fufu Fang <fangfufu2003@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the OpenSSL
 * library.
 */

/**
 * \file log.c
 * \brief Logging system implementation
 */

#include "log.h"

#include "config.h"
#include "util.h"

#include <curl/curl.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

int log_level_init(void)
{
    char *env = getenv("HTTPDIRFS_LOG_LEVEL");
    if (env) {
        return (int)strtol(env, NULL, 10);
    }
#ifdef DEBUG
    return DEFAULT_LOG_LEVEL | debug;
#else
    return DEFAULT_LOG_LEVEL;
#endif
}

static void __attribute__((format(printf, 5, 0)))
vlog_printf(LogType type, const char *file, const char *func, int line,
            const char *format, va_list args)
{
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
            fprintf(stderr, "(%x)", (unsigned int)type);
        }
        fprintf(stderr, ":");
        break;
    }

    fprintf(stderr, "%s:%d:", file, line);

print_actual_message: {
}
    fprintf(stderr, "%s: ", func);
    vfprintf(stderr, format, args);
}

void log_printf(LogType type, const char *file, const char *func, int line,
                const char *format, ...)
{
    if (type & CONFIG.log_type) {
        va_list args;
        va_start(args, format);
        vlog_printf(type, file, func, line, format, args);
        va_end(args);
    }
}

void fatal_log_printf(const char *file, const char *func, int line,
                      const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vlog_printf(fatal, file, func, line, format, args);
    va_end(args);
    exit_failure();
}

void print_version(void)
{
    /* FUSE prints its help to stderr */
    fprintf(stderr, "HTTPDirFS version " VERSION "\n");
    /*
     * --------- Print off SSL engine version ---------
     */
    curl_version_info_data *data = curl_version_info(CURLVERSION_NOW);
    fprintf(stderr, "libcurl SSL engine: %s\n", data->ssl_version);
}
