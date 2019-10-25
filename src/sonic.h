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
 * \brief Create a new Sonic LinkTable in index mode)
 */
LinkTable *sonic_LinkTable_new_index_mode(const int id);

/**
 * \brief Create a new Sonic LinkTable in ID3 mode)
 */
LinkTable *sonic_LinkTable_new_id3_mode(const char *sonic_id_str);

#endif
