#include "nbtReader.h"
#include "world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <lz4.h>
#include <errno.h>

#define SECTOR_SIZE 4096
#define REGION_SIZE 32
#define MAX_PATH 4096

// some math stuff first
// divide by 32 and round down
static int32_t floor_div32(int32_t value) {
    int64_t v = value;

    return (int32_t) (v >= 0 ? v / 32 : -((-v + 31) / 32));
}

// used to convert a chunk coordinate to its region-local coordinate
static int32_t mod32(int32_t value) {
    int32_t result = value % 32;

    return result < 0 ? result + 32 : result;
}

// read a bigendian uint32 from a region file
static uint32_t read_u32(FILE *file) {
    unsigned char b[4];

    if (fread(b, 1, sizeof(b), file) != sizeof(b)) return 0;

    return ((uint32_t) b[0] << 24) | ((uint32_t) b[1] << 16) | ((uint32_t) b[2] << 8) | b[3]; // please
}

static uint32_t read_le32(const unsigned char *data) {
    return (uint32_t) data[0] | ((uint32_t) data[1] << 8) | ((uint32_t) data[2] << 16) | ((uint32_t) data[3] << 24);
}

// decompress zlib/gzip data into a buffer that can be expanded TODO
static int inflateData(const unsigned char *input, size_t inputSize, int gzip, unsigned char **output, size_t *outputSize) {
    z_stream stream = {0};
    size_t capacity = 262144; // 256 * 1024
    unsigned char *buffer = malloc(capacity);

    if (!buffer) return 0;

    stream.next_in = (Bytef*) input;
    stream.avail_in = (uInt) inputSize;

    if (inflateInit2(&stream, gzip ? 15 + 16 : 15) != Z_OK) {
        free(buffer);

        return 0;
    }

    for (;;) {
        if (stream.total_out == capacity) {
            size_t newCapacity = capacity * 2;
            unsigned char *newBuffer = realloc(buffer, newCapacity);

            if (!newBuffer) {
                inflateEnd(&stream);
                free(buffer);

                return 0;
            }

            buffer = newBuffer;
            capacity = newCapacity;
        }

        stream.next_out = buffer + stream.total_out;
        stream.avail_out = (uInt) capacity - stream.total_out;

        int result = inflate(&stream, Z_NO_FLUSH);
        if (result == Z_STREAM_END) break;

        if (result != Z_OK) {
            inflateEnd(&stream);
            free(buffer);

            return 0;
        }
    }

    *output = buffer;
    *outputSize = stream.total_out;

    inflateEnd(&stream);

    return 1;
}

// Decompress alt LZ4 block format
static int inflateLZ4(const unsigned char *input, size_t inputSize, unsigned char **output, size_t *outputSize) {
    if (inputSize < 21 || memcmp(input, "LZ4Block", 8) != 0) return 0;

    unsigned char method = input[8] & 0xF0; // extracts the upper 4 bits of the 8th byte to check the compression flags
    int32_t compressSize = (int32_t) ((uint32_t) input[9] | ((uint32_t) input[10] << 8) | ((uint32_t) input[11] << 16) | ((uint32_t) input[12] << 24));
    int32_t decompressSize = (int32_t) ((uint32_t) input[13] | ((uint32_t) input[14] << 8) | ((uint32_t) input[15] << 16) | ((uint32_t) input[16] << 24));

    if (compressSize < 0 || decompressSize < 0 || (size_t) compressSize > inputSize - 21) return 0;

    unsigned char *buffer = malloc((size_t) decompressSize);
    if (!buffer) return 0;

    const unsigned char *payload = input + 21; // header offset
    int result;

    if (method == 0x10) { // raw; data not compressed
        if (compressSize != decompressSize) {
            free(buffer);

            return 0;
        }

        memcpy(buffer, payload, (size_t) decompressSize);

        result = decompressSize;
    } else if (method == 0x20) { // LZ4 compressed
        result = LZ4_decompress_safe((const char *) payload, (char *) buffer, compressSize, decompressSize);
    } else {
        free(buffer);

        return 0;
    }

    if (result != decompressSize) {
        free(buffer);

        return 0;
    }

    *output = buffer;
    *outputSize = (size_t) decompressSize;

    return 1;
}

static int decompressChunk(uint8_t compression, const unsigned char *input, size_t inputSize, unsigned char **output, size_t *outputSize) {
    switch (compression) {
        case 1:
            return inflateData(input, inputSize, 1, output, outputSize);
        case 2:
            return inflateData(input, inputSize, 0, output, outputSize);
        case 3:
            *output = malloc(inputSize);

            if (!*output) return 0;

            memcpy(*output, input, inputSize);
            *outputSize = inputSize;

            return 1;
        case 4:
            return inflateLZ4(input, inputSize, output, outputSize);
        default:
            return 0;
    }
}

static int scanChunk(FILE *file, const char *regionPath, int32_t chunkX, int32_t chunkZ, int localX, int localZ, WorldEntityCallback callback, void *context) {
    int index = localX + localZ * REGION_SIZE;

    if (fseek(file, index * 4L, SEEK_SET) != 0) return 0;

    uint32_t location = read_u32(file);
    if (!location) return 1;

    uint32_t sectorOffset = location >> 8;
    uint32_t sectorCount = location & 0xFF;
    if (!sectorOffset || !sectorCount) return 1;

    if (fseek(file, (long) sectorOffset * SECTOR_SIZE, SEEK_SET) != 0) return 0;

    uint32_t length = read_u32(file);
    if (length < 1 || length > sectorCount * SECTOR_SIZE - 4) return 0;

    uint8_t compression;
    if (fread(&compression, 1, 1, file) != 1) return 0;

    size_t payloadSize = length - 1;
    unsigned char *payload = malloc(payloadSize);

    if (!payload) return 0;
    if (fread(payload, 1, payloadSize, file) != payloadSize) {
        free(payload);

        return 0;
    }

    if (compression & 0x80) {
        free(payload);

        char extChunkPath[MAX_PATH];

        snprintf(extChunkPath, sizeof(extChunkPath), "%.*s/c.%d.%d.mcc", (int) (strrchr(regionPath, '/') - regionPath), regionPath, chunkX, chunkZ);

        FILE *extChunkFile = fopen(extChunkPath, "rb");
        if (!extChunkFile) return 1;

        fseek(extChunkFile, 0, SEEK_END);

        long extChunkSize = ftell(extChunkFile);

        fseek(extChunkFile, 0, SEEK_SET);
        if (extChunkSize <= 0) {
            fclose(extChunkFile);

            return 0;
        }

        payloadSize = (size_t) extChunkSize;
        payload = malloc(payloadSize);
        if (!payload || fread(payload, 1, payloadSize, extChunkFile) != payloadSize) {
            fclose(extChunkFile);
            free(payload);

            return 0;
        }

        fclose(extChunkFile);

        compression &= 0x7F; // mask off 0x80 flag to isolate algorithm (1 = GZip, 2 = Zlib, 4 = LZ4)
    }

    unsigned char *nbt = NULL;
    size_t nbtSize = 0;
    int ok = decompressChunk(compression, payload, payloadSize, &nbt, &nbtSize);

    free(payload);
    if (!ok) return 0;

    ok = nbtFindBlockEntities(nbt, nbtSize, callback, context);

    free(nbt);

    return ok;
}

// scan the requested chunks inside a region file
static int scanRegion(const char *worldPath, int32_t regionX, int32_t regionZ, int32_t minX, int32_t minZ, int32_t maxX, int32_t maxZ, WorldEntityCallback callback, void *context) {
    char path[MAX_PATH];

    snprintf(path, sizeof(path), "%s/dimensions/minecraft/overworld/region/r.%d.%d.mca", worldPath, regionX, regionZ);

    FILE *file = fopen(path, "rb");
    if (!file) {
        if (errno == ENOENT) return 1;

        fprintf(stderr, "HEY! Could not open %s: %s\n", path, strerror(errno));

        return 0;
    }

    int32_t startX = minX > regionX * 32 ? minX : regionX * 32;
    int32_t endX = maxX < regionX * 32 + 31 ? maxX : regionX * 32 + 31;
    int32_t startZ = minZ > regionZ * 32 ? minZ : regionZ * 32;
    int32_t endZ = maxZ < regionZ * 32 + 31 ? maxZ : regionZ * 32 + 31;

    for (int32_t z = startZ; z <= endZ; ++z)
        for (int32_t x = startX; x <= endX; ++x)
            if (!scanChunk(file, path, x, z, mod32(x), mod32(z), callback, context))
                fprintf(stderr, "Oh noes! Could not parse the chunk %d %d in %s\n", x, z, path);

    fclose(file);
    return 1;
}

// scan every region touched by the selected area
int worldScan(const char *worldPath, int32_t minChunkX, int32_t minChunkZ, int32_t maxChunkX, int32_t maxChunkZ, WorldEntityCallback callback, void *context) {
    int32_t firstRegionX = floor_div32(minChunkX);
    int32_t lastRegionX = floor_div32(maxChunkX);
    int32_t firstRegionZ = floor_div32(minChunkZ);
    int32_t lastRegionZ = floor_div32(maxChunkZ);

    for (int32_t regionZ = firstRegionZ; regionZ <= lastRegionZ; ++regionZ)
        for (int32_t regionX = firstRegionX; regionX <= lastRegionX; ++regionX)
            if (!scanRegion(worldPath, regionX, regionZ, minChunkX, minChunkZ, maxChunkX, maxChunkZ, callback, context)) return 0;

    return 1;
}