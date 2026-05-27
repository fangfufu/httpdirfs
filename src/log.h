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

#ifndef LOG_H
#define LOG_H
/**
 * \file log.h
 * \brief Logging system macros and levels header
 */


/**
 * \brief Log types
 */
typedef enum {
    fatal = 1 << 0,
    error = 1 << 1,
    warning = 1 << 2,
    info = 1 << 3,
    debug = 1 << 4,
    link_lock_debug = 1 << 5,
    network_lock_debug = 1 << 6,
    cache_lock_debug = 1 << 7,
    libcurl_debug = 1 << 8,
} LogType;

/**
 * \brief The default log level
 */
#define DEFAULT_LOG_LEVEL (fatal | error | warning | info)

/**
 * \brief Get the log level from the environment.
 */
int log_level_init(void);

/**
 * \brief Log printf
 * \details This is for printing nice log messages
 */
void log_printf(LogType type, const char *file, const char *func, int line,
                const char *format, ...) __attribute__((format(printf, 5, 6)));

/**
 * \brief Fatal log printf
 * \details This is for printing fatal error messages and exiting
 */
_Noreturn void fatal_log_printf(const char *file, const char *func, int line,
                                const char *format, ...)
    __attribute__((format(printf, 4, 5)));

/**
 * \brief Log type printf
 * \details This macro automatically prints out the filename and line number
 */
#define lprintf(type, ...)                                                     \
    do {                                                                       \
        LogType _l_type = (type);                                              \
        if (_l_type & fatal)                                                   \
            fatal_log_printf(__FILE__, __func__, __LINE__, __VA_ARGS__);       \
        else                                                                   \
            log_printf(_l_type, __FILE__, __func__, __LINE__, __VA_ARGS__);    \
    } while (0)

/**
 * \brief Print the version information for HTTPDirFS
 */
void print_version(void);

#endif
