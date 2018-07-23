#ifndef DATA_H
#define DATA_H
/**
 * \file data.h
 * \brief This header stores all the custom data type definition
 */

#include <stdlib.h>

/** \brief use this data type for buffer */
typedef struct BufferStruct BufferStruct;

struct BufferStruct {
    size_t size;
    char *data;
};

#endif
