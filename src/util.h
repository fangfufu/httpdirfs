#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

/**
 * \file util.h
 * \brief utility functions
 */

/**
 * \brief strndup with concatenation
 * \details This function concatenate string a and string b together, and put
 * the result in a new string.
 * \param[in] a the first string
 * \param[in] b the second string
 * \param[in] n the maximum length of the output string
 */
char *strndupcat(const char *a, const char *b, int n);

/**
 * \brief division, but rounded to the nearest integer rather than truncating
 */
int64_t round_div(int64_t a, int64_t b);

#endif
