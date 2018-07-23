#ifndef DATA_H
#define DATA_H
/**
 * \file data.h
 * \brief This header stores all the custom data type definition
 */

#include <stdlib.h>

/** \brief use this data type for buffer */
typedef struct BufferStruct BufferStruct;

struct MemoryStruct {
    char *memory;
    size_t size;
};

typedef struct MemoryStruct MemoryStruct;

#endif
