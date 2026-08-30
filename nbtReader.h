#define NBTREADER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const unsigned char *data;

    size_t size;
    size_t pos;
} NbtReader;

typedef void (*BlockEntityCallback)(const char *id, int32_t x, int32_t y, int32_t z, void *context);

int nbtFindBlockEntities(const unsigned char *data, size_t size, BlockEntityCallback callback, void *context);