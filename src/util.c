#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *strndupcat(const char *a, const char *b, int n)
{
    int na = strnlen(a, n/2);
    int nb = strnlen(b, n/2);
    int nc = na + nb + 1;
    char *c = calloc(nc, sizeof(char));
    if (!c) {
        fprintf(stderr, "strndupcat(): calloc failure!\n");
        exit(EXIT_FAILURE);
    }
    strncpy(c, a, na);
    strncat(c, b, nb);
    return c;
}
