#include <stdint.h>
#include <stdio.h>

// some math stuff

static int32_t floor_div32(int32_t value) {
    int64_t v = value;

    return (int32_t) (v >= 0 ? v / 32 : -((-v + 31) / 32));
}

// used to convert a chunk coordinate to its region-local coordinate.
static int32_t mod32(int32_t value) {
    int32_t result = value % 32;

    return result < 0 ? result + 32 : result;
}

// read one big-endian uint32 from a region file
static uint32_t read_u32(FILE *file) {
    unsigned char b[4];

    if (fread(b, 1, sizeof(b), file) != sizeof(b)) return 0;

    return ((uint32_t) b[0] << 24) | ((uint32_t) b[1] << 16) | ((uint32_t) b[2] << 8) | b[3];
}