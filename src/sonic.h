#ifndef SONIC_H
#define SONIC_H
/**
 * \file sonic.h
 * \brief Sonic related function
 */

typedef struct {
    /**
     * \brief Sonic id field
     * \details This is used to store the followings:
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

#include "link.h"

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
