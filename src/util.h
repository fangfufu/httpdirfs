#ifndef UTIL_H
#define UTIL_H

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
 * \param[n] c the maximum length of the output string
 */
char *strndupcat(const char *a, const char *b, int n);

#endif
