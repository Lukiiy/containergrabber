#include <stdio.h>

#include <stdint.h>
#include <stdlib.h>

static int parse_int(const char *text, int32_t *value) {
    char *end;
    long long parsed = strtoll(text, &end, 10);

    if (*text == '\0' || *end != '\0' || parsed < INT32_MIN || parsed > INT32_MAX) return 0;
    *value = (int32_t) parsed;

    return 1;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <world> <chunkX1> <chunkZ1> <chunkX2> <chunkZ2>\n", argv[0]);

        return 0;
    }

    int32_t x1;
    int32_t z1;
    int32_t x2;
    int32_t z2;

    if (!parse_int(argv[2], &x1) || !parse_int(argv[3], &z1) || !parse_int(argv[4], &x2) || !parse_int(argv[5], &z2)) {
        fprintf(stderr, "Invalid chunk coordinate!\n");

        return 0;
    }

    int32_t min_x = x1 < x2 ? x1 : x2;
    int32_t max_x = x1 > x2 ? x1 : x2;
    int32_t min_z = z1 < z2 ? z1 : z2;
    int32_t max_z = z1 > z2 ? z1 : z2;

    fprintf(stderr, "mins & maxes: %d %d %d %d\n", min_x, min_z, max_x, max_z);

    return 0;
}