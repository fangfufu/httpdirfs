#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *strndupcat(const char *a, const char *b, int n)
{
    int na = strnlen(a, n);
    int nb = strnlen(b, n);
    int nc = na + nb + 1;
    if (nc > n) {
        fprintf(stderr,
            "strndupcat(): resulting string length exceeds maximum limit!\n");
        /*
         * It is better to crash the program here, then corrupting the cache
         * folder
         */
        exit(EXIT_FAILURE);
    }
    char *c = calloc(nc, sizeof(char));
    if (!c) {
        fprintf(stderr, "strndupcat(): calloc failure!\n");
        exit(EXIT_FAILURE);
    }
    strncpy(c, a, na);
    strncat(c, b, nb);
    c[nc-1] = '\0';
    return c;
}
