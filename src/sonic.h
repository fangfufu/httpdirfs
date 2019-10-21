#ifndef SONIC_H
#define SONIC_H
/**
 * \file sonic.h
 * \brief Subsonic related function
 */

#include "link.h"

/**
 * \brief Initialise SubSonic configuration.
 */
void sonic_config_init(const char *server, const char *username,
                       const char *password);
#endif
