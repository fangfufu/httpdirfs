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
LinkTable *sonic_LinkTable_new_index_mode(const int id);

/**
 * \brief Create a new Sonic LinkTable in ID3 mode
 * \details In this mode, the filesystem effectively has 4 levels of
 * directories, which are:
 *  1. Root
 *  2. Index
 *  3. Artists
 *  4. Album
 */
LinkTable *sonic_LinkTable_new_id3_mode(char *sonic_id_str);

#endif
