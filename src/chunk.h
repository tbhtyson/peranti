#ifndef PERANTI_CHUNK_H
#define PERANTI_CHUNK_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  uint16_t content_id;
  uint8_t param1;
  uint8_t param2;
} Node;

typedef enum {
  MAPBLOCK_UNIFORM, // whole block is one Node; .nodes is not allocated
  MAPBLOCK_FULL,    // .nodes is a heap-allocated 4096-entry array
} MapblockKind;

typedef struct {
  MapblockKind kind;
  Node single;  // valid when kind == MAPBLOCK_UNIFORM
  Node *nodes;  // valid when kind == MAPBLOCK_FULL; flat, wire order
} Mapblock;

// Linear index for node (x,y,z) within a mapblock, matching Luanti's own
// z*16*16 + y*16 + x wire-format ordering, so mapblock_decode.c can memcpy
// straight from decompressed bytes into `nodes` with no reshaping.
static inline int mapblock_index(int x, int y, int z) {
  if (x < 0 || x >= 16 || y < 0 || y >= 16 || z < 0 || z >= 16) {
    fprintf(stderr, "mapblock_index: (%d,%d,%d) out of range\n", x, y, z);
    exit(1);
  }
  return z * 16 * 16 + y * 16 + x;
}

Mapblock mapblock_make_uniform(Node value);
Mapblock mapblock_make_full(void); // .nodes allocated, contents undefined --
                                    // caller (mapblock_decode.c) fills it
void mapblock_destroy(Mapblock *mb);

Node mapblock_get(const Mapblock *mb, int x, int y, int z);
void mapblock_set(Mapblock *mb, int x, int y, int z, Node value);

#endif
