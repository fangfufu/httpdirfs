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
 * \brief Create a new Sonic LinkTable
 */
LinkTable *sonic_LinkTable_new(const int id);
#endif
