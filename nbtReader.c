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
static int has(NbtReader *reader, size_t n) {
    return n <= reader -> size - reader -> pos;
}

// Read one byte from the NBT stream
static int read_u8(NbtReader *reader, uint8_t *value) {
    if (!has(reader, 1)) return 0;

    *value = reader -> data[reader -> pos++];

    return 1;
}

// Read a big-endian NBT int
static int read_i32(NbtReader *reader, int32_t *value) {
    if (!has(reader, 4)) return 0;

    uint32_t rawValue = ((uint32_t) reader -> data[reader -> pos] << 24) | ((uint32_t) reader -> data[reader -> pos + 1] << 16) | ((uint32_t) reader -> data[reader -> pos + 2] << 8) | ((uint32_t) reader -> data[reader -> pos + 3]); // assemble a 32-bit big-endian integer from 4 sequential bytes

    reader -> pos += 4;
    *value = (int32_t) rawValue;

    return 1;
}

// read an NBT string into a fixed buffer.
static int read_string(NbtReader *reader, char *out, size_t capacity) {
    if (!has(reader, 2)) return 0;

    uint16_t len = ((uint16_t) reader -> data[reader -> pos] << 8) | reader -> data[reader -> pos + 1]; // read the 16-bit big-endian string length header

    reader -> pos += 2;

    if (!has(reader, len) || len >= capacity) return 0;
    memcpy(out, reader -> data + reader -> pos, len);

    out[len] = '\0'; // null-terminator byte fire emoji
    reader -> pos += len;

    return 1;
}

// skip an NBT string without copying it
static int skip_string(NbtReader *reader) {
    if (!has(reader, 2)) return 0;

    uint16_t len = ((uint16_t) reader -> data[reader -> pos] << 8) | reader -> data[reader -> pos + 1]; // big endian string byte length

    reader -> pos += 2;

    if (!has(reader, len)) return 0;
    reader -> pos += len;

    return 1;
}

static int skip_payload(NbtReader *reader, uint8_t type);

// skip every tag inside a compound
static int skip_compound(NbtReader *reader) {
    for (;;) {
        uint8_t type;

        if (!read_u8(reader, &type)) return 0;

        if (type == TAG_END) return 1;

        if (!skip_string(reader) || !skip_payload(reader, type)) return 0;
    }
}

// skip every element of an NBT list
static int skip_list(NbtReader *reader) {
    uint8_t type;
    int32_t count;

    if (!read_u8(reader, &type) || !read_i32(reader, &count) || count < 0) return 0;

    for (int32_t i = 0; i < count; ++i) {
        if (!skip_payload(reader, type)) return 0;
    }

    return 1;
}

// skip a payload based on its tag type
static int skip_payload(NbtReader *reader, uint8_t type) {
    size_t size;

    switch (type) {
        case TAG_BYTE:
            size = 1;
            break;
        case TAG_SHORT:
            size = 2;
            break;
        case TAG_FLOAT:
        case TAG_INT:
            size = 4;
            break;
        case TAG_DOUBLE:
        case TAG_LONG:
            size = 8;
            break;
        case TAG_STRING:
            return skip_string(reader);
        case TAG_LIST:
            return skip_list(reader);
        case TAG_COMPOUND:
            return skip_compound(reader);
        case TAG_BYTE_ARRAY:
        case TAG_INT_ARRAY:
        case TAG_LONG_ARRAY: {
            int32_t count;
            size_t objSize = type == TAG_BYTE_ARRAY ? 1 : type == TAG_INT_ARRAY ? 4 : 8;

            if (!read_i32(reader, &count) || count < 0) return 0;
            size = (size_t) count * objSize;

            if (!has(reader, size)) return 0;

            reader -> pos += size;
            return 1;
        }

        default: return 0;
    }

    if (!has(reader, size)) return 0;

    reader -> pos += size;
    return 1;
}

// read one block entity and extract its id and position
static int readBlockEntity(NbtReader *reader, char *id, size_t idSize, int32_t *x, int32_t *y, int32_t *z) {
    int hasId = 0;
    int hasX = 0;
    int hasY = 0;
    int hasZ = 0;

    for (;;) {
        uint8_t type;

        if (!read_u8(reader, &type)) return 0;
        if (type == TAG_END) break;

        char name[64];
        if (!read_string(reader, name, sizeof(name))) return 0;

        if (type == TAG_STRING && strcmp(name, "id") == 0) {
            if (!read_string(reader, id, idSize)) return 0;

            hasId = 1;
        } else if (type == TAG_INT && strcmp(name, "x") == 0) {
            if (!read_i32(reader, x)) return 0;

            hasX = 1;
        } else if (type == TAG_INT && strcmp(name, "y") == 0) {
            if (!read_i32(reader, y)) return 0;

            hasY = 1;
        } else if (type == TAG_INT && strcmp(name, "z") == 0) {
            if (!read_i32(reader, z)) return 0;

            hasZ = 1;
        } else if (!skip_payload(reader, type)) {
            return 0;
        }
    }

    if (!hasId || !hasX || !hasY || !hasZ) return 0;

    return 1;
}

// read block entities thingy and send each entry to the callback
static int readBlockEntityList(NbtReader *reader, BlockEntityCallback callback, void *context) {
    uint8_t objType;
    int32_t count;

    if (!read_u8(reader, &objType) || !read_i32(reader, &count) || count < 0) return 0;
    if (count > 0 && objType != TAG_COMPOUND) return 0;

    for (int32_t i = 0; i < count; ++i) {
        char id[128];
        int32_t x;
        int32_t y;
        int32_t z;

        if (!readBlockEntity(reader, id, sizeof(id), &x, &y, &z)) return 0;

        callback(id, x, y, z, context);
    }

    return 1;
}

// find the chunk's block_entities list without decoding block states
int nbtFindBlockEntities(const unsigned char *data, size_t size, BlockEntityCallback callback, void *context) {
    NbtReader reader = { data, size, 0 };
    uint8_t rootType;

    if (!read_u8(&reader, &rootType) || rootType != TAG_COMPOUND || !skip_string(&reader)) return 0;

    for (;;) {
        uint8_t type;

        if (!read_u8(&reader, &type)) return 0;
        if (type == TAG_END) return 1;

        char name[64];
        if (!read_string(&reader, name, sizeof(name))) return 0;

        if (type == TAG_LIST && strcmp(name, "block_entities") == 0) {
            if (!readBlockEntityList(&reader, callback, context)) return 0;
        } else if (!skip_payload(&reader, type)) {
            return 0;
        }
    }
}
