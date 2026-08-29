#include "nbtReader.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    TAG_END = 0,
    TAG_BYTE = 1,
    TAG_SHORT = 2,
    TAG_INT = 3,
    TAG_LONG = 4,
    TAG_FLOAT = 5,
    TAG_DOUBLE = 6,
    TAG_BYTE_ARRAY = 7,
    TAG_STRING = 8,
    TAG_LIST = 9,
    TAG_COMPOUND = 10,
    TAG_INT_ARRAY = 11,
    TAG_LONG_ARRAY = 12
};

// Check that n bytes are still available
static int has(NbtReader *r, size_t n) {
    return n <= r -> size - r -> pos;
}

// Read one byte from the NBT stream
static int read_u8(NbtReader *r, uint8_t *value) {
    if (!has(r, 1)) return 0;

    *value = r -> data[r -> pos++];

    return 1;
}

// Read a big-endian NBT int
static int read_i32(NbtReader *r, int32_t *value) {
    if (!has(r, 4)) return 0;

    uint32_t v = ((uint32_t) r -> data[r -> pos] << 24) | ((uint32_t) r -> data[r -> pos + 1] << 16) | ((uint32_t) r -> data[r -> pos + 2] << 8) | ((uint32_t) r -> data[r -> pos + 3]);

    r -> pos += 4;
    *value = (int32_t) v;

    return 1;
}

// read an NBT string into a fixed buffer.
static int read_string(NbtReader *r, char *out, size_t capacity) {
    if (!has(r, 2)) return 0;

    uint16_t len = ((uint16_t) r -> data[r -> pos] << 8) | r -> data[r -> pos + 1];

    r -> pos += 2;

    if (!has(r, len) || len >= capacity) return 0;
    memcpy(out, r -> data + r -> pos, len);

    out[len] = '\0';
    r -> pos += len;

    return 1;
}