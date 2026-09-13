#ifndef PERANTI_WORLD_H
#define PERANTI_WORLD_H
#include "chunk.h"
#include <stdbool.h>
#include <stdint.h>

// Luanti mapblock coordinates are signed and go negative in every axis.
typedef struct {
  int32_t x, y, z;
} ChunkCoord;

static inline bool chunkcoord_equal(ChunkCoord a, ChunkCoord b) {
  return a.x == b.x && a.y == b.y && a.z == b.z;
}

typedef enum {
  SLOT_EMPTY,
  SLOT_OCCUPIED,
  // no SLOT_TOMBSTONE yet -- that's a world_remove problem, and
  // world_remove doesn't exist yet. Decide this when it does; don't add it
  // speculatively now.
} SlotState;

typedef struct {
  SlotState state;
  ChunkCoord coord;
  Mapblock block;
} WorldSlot;

typedef struct {
  WorldSlot *slots;
  uint32_t capacity; // fixed at world_init(), never grows
  uint32_t count;    // occupied slots, for load-factor checks
} World;

void world_init(World *w, uint32_t capacity);
void world_destroy(World *w);

// NULL means "not loaded" -- absence from the table is the sparsity
// mechanism (see Phase 1), not a separate flag to check first.
Mapblock *world_get(World *w, ChunkCoord coord);

bool world_insert(World *w, ChunkCoord coord, Mapblock block);

// world_remove intentionally not declared yet -- needs the tombstone
// decision above settled first. Add it when Phase 5's eviction path
// actually needs it, not before.

#endif
