#define NBTREADER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const unsigned char *data;

    size_t size;
    size_t pos;
} NbtReader;