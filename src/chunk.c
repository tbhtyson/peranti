#include "chunk.h"
#include <stdbool.h>
#include <string.h>

Mapblock mapblock_make_uniform(Node value) {
  return (Mapblock){.kind = MAPBLOCK_UNIFORM, .single = value, .nodes = NULL};
}

Mapblock mapblock_make_full(void) {
  Node *nodes = malloc(sizeof(Node) * 16 * 16 * 16);
  if (!nodes) {
    fprintf(stderr, "mapblock_make_full: out of memory\n");
    exit(1);
  }
  return (Mapblock){.kind = MAPBLOCK_FULL, .nodes = nodes};
}

void mapblock_destroy(Mapblock *mb) {
  if (mb->kind == MAPBLOCK_FULL) {
    free(mb->nodes);
    mb->nodes = NULL;
  }
}

// Expands a uniform block into a full array, all 4096 entries set to the
// value it was uniform on. Only called from mapblock_set() below, and only
// when the incoming write would actually break uniformity -- a uniform
// block never densifies just from being read.
static void mapblock_densify(Mapblock *mb) {
  Node *nodes = malloc(sizeof(Node) * 16 * 16 * 16);
  if (!nodes) {
    fprintf(stderr, "mapblock_densify: out of memory\n");
    exit(1);
  }
  for (int i = 0; i < 16 * 16 * 16; i++) {
    nodes[i] = mb->single;
  }
  mb->kind = MAPBLOCK_FULL;
  mb->nodes = nodes;
}

Node mapblock_get(const Mapblock *mb, int x, int y, int z) {
  if (mb->kind == MAPBLOCK_UNIFORM) {
    (void)mapblock_index(x, y, z); // range-check (x,y,z) even though a
                                    // uniform block's answer doesn't depend
                                    // on them -- out-of-range coordinates
                                    // are a caller bug either way
    return mb->single;
  }
  return mb->nodes[mapblock_index(x, y, z)];
}

void mapblock_set(Mapblock *mb, int x, int y, int z, Node value) {
  if (mb->kind == MAPBLOCK_UNIFORM) {
    bool same = value.content_id == mb->single.content_id &&
                value.param1 == mb->single.param1 &&
                value.param2 == mb->single.param2;
    if (same) {
      return; // still uniform, nothing to do
    }
    mapblock_densify(mb);
  }
  mb->nodes[mapblock_index(x, y, z)] = value;
}
