#define WORLD_H

#include <stdint.h>

typedef void (*WorldEntityCallback)(const char *id, int32_t x, int32_t y, int32_t z, void *context);

int worldScan(const char *world_path, int32_t min_chunk_x, int32_t min_chunk_z, int32_t max_chunk_x, int32_t max_chunk_z, WorldEntityCallback callback, void *context);
