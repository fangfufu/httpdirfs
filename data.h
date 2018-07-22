#ifndef DATA_H
#define DATA_H
/**
 * \file data.h
 * \brief This header stores all the custom data type definition
 */

#include <stdlib.h>

/** \brief use this data type for buffer */
typedef struct MemoryStruct MemoryStruct;

struct MemoryStruct {
    size_t size;
    char *memory;
};

void MemoryStruct_free(MemoryStruct *ms);

#endif
