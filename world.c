#include "nbtReader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <lz4.h>

// read a bigendian uint32 from a region file
static uint32_t read_u32(FILE *file) {
    unsigned char b[4];

    if (fread(b, 1, sizeof(b), file) != sizeof(b)) return 0;

    return ((uint32_t) b[0] << 24) | ((uint32_t) b[1] << 16) | ((uint32_t) b[2] << 8) | b[3]; // please
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