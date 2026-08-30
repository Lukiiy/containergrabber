#include "world.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static int parseInt(const char *text, int32_t *value) {
    char *end;
    long long parsed = strtoll(text, &end, 10);

    if (*text == '\0' || *end != '\0' || parsed < INT32_MIN || parsed > INT32_MAX) return 0;
    *value = (int32_t) parsed;

    return 1;
}


static void displayContainers(const char *id, int32_t x, int32_t y, int32_t z, void *context) {
    (void) context;

    if (strcmp(id, "minecraft:chest") == 0) printf("chest: %d %d %d\n", x, y, z); else if (strcmp(id, "minecraft:barrel") == 0) printf("barrel: %d %d %d\n", x, y, z);
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <world> <chunkX1> <chunkZ1> <chunkX2> <chunkZ2>\n", argv[0]);

        return 1;
    }

    int32_t x1;
    int32_t z1;
    int32_t x2;
    int32_t z2;

    if (!parseInt(argv[2], &x1) || !parseInt(argv[3], &z1) || !parseInt(argv[4], &x2) || !parseInt(argv[5], &z2)) {
        fprintf(stderr, "Invalid chunk coordinate!\n");

        return 1;
    }

    int32_t min_x = x1 < x2 ? x1 : x2;
    int32_t max_x = x1 > x2 ? x1 : x2;
    int32_t min_z = z1 < z2 ? z1 : z2;
    int32_t max_z = z1 > z2 ? z1 : z2;

    return worldScan(argv[1], min_x, min_z, max_x, max_z, displayContainers, NULL) ? 0 : 1;
}