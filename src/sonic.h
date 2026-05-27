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

#ifndef SONIC_H
#define SONIC_H
/**
 * \file sonic.h
 * \brief Sonic related function
 */

typedef struct {
    /**
     * \brief Sonic id field
     * \details This is used to store the following:
     *  - Arist ID
     *  - Album ID
     *  - Song ID
     *  - Sub-directory ID (in the XML response, this is the ID on the "child"
     *    element)
     */
    char *id;
    /**
     * \brief Sonic directory depth
     * \details This is used exclusively in ID3 mode to store the depth of the
     * current directory.
     */
    int depth;
} Sonic;

typedef struct LinkTable LinkTable;

/**
 * \brief Initialise Sonic configuration.
 */
void sonic_config_init(const char *server, const char *username,
                       const char *password);

/**
 * \brief Create a new Sonic LinkTable in index mode
 */
LinkTable *sonic_LinkTable_new_index(const char *id);

/**
 * \brief Create a new Sonic LinkTable in ID3 mode
 * \details In this mode, the filesystem effectively has 5 levels of which are:
 *  0. Root table
 *  1. Index table
 *  2. Artist table
 *  3. Album table
 *  4. Song table
 *  5. Individual song (not a table)
 * \param[in] depth the level of the requested table
 * \param[in] id the id of the requested table
 */
LinkTable *sonic_LinkTable_new_id3(int depth, const char *id);

#endif
