#ifndef SONIC_H
#define SONIC_H
/**
 * \file sonic.h
 * \brief Sonic related function
 */

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
 *  5. Individual song
 * \param[in] depth the level of the requested table
 * \param[in] id the id of the requested table
 */
LinkTable *sonic_LinkTable_new_id3(int depth, const char *id);

#endif
