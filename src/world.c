#include "world.h"
#include <stdio.h>
#include <stdlib.h>

// Bit-mixer (murmur3-style finalizer): avalanches a single 32-bit value so
// small input changes flip roughly half the output bits. Casting the
// signed ChunkCoord fields to uint32_t before this runs sidesteps
// implementation-defined signed right-shift entirely -- two's-complement
// negative values just become large unsigned ones, and unsigned arithmetic
// wraps predictably, which is all a hash needs.
static uint32_t mix32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

// Combines all three axes -- hashing fewer than three would collapse a
// whole vertical column (same x,z, varying y -- the common case when a
// player stands in one spot) into a single bucket. Each axis is mixed
// independently, then multiplied by a different large odd constant before
// XOR so the three don't cancel each other out on structured input (e.g.
// coords that differ only in one axis, which is most real chunk-load
// traffic).
static uint32_t world_hash(ChunkCoord coord, uint32_t capacity) {
  uint32_t h = mix32((uint32_t)coord.x) ^
               mix32((uint32_t)coord.y) * 0x9e3779b9U ^
               mix32((uint32_t)coord.z) * 0x85ebca6bU;
  // % works for any capacity; if you size world_init() with a power-of-two
  // capacity you can replace this with `h & (capacity - 1)` instead (no
  // division). Left as % for now since capacity isn't constrained to be a
  // power of two.
  return h % capacity;
}

void world_init(World *w, uint32_t capacity) {
  // calloc zero-initializes every slot's state to SLOT_EMPTY (0), so no
  // separate init loop is needed.
  w->slots = calloc(capacity, sizeof(WorldSlot));
  if (!w->slots) {
    fprintf(stderr, "world_init: out of memory (capacity %u)\n", capacity);
    exit(1);
  }
  w->capacity = capacity;
  w->count = 0;
}

void world_destroy(World *w) {
  for (uint32_t i = 0; i < w->capacity; i++) {
    if (w->slots[i].state == SLOT_OCCUPIED) {
      mapblock_destroy(&w->slots[i].block);
    }
  }
  free(w->slots);
  w->slots = NULL;
  w->capacity = 0;
  w->count = 0;
}

Mapblock *world_get(World *w, ChunkCoord coord) {
  uint32_t i = world_hash(coord, w->capacity);
  for (uint32_t probed = 0; probed < w->capacity; probed++) {
    WorldSlot *slot = &w->slots[i];
    if (slot->state == SLOT_EMPTY) {
      // No tombstones exist yet (see world.h), so an empty slot really
      // does mean the probe chain ends here -- this line has to change
      // the day world_remove exists without also introducing a
      // SLOT_TOMBSTONE state.
      return NULL;
    }
    if (slot->state == SLOT_OCCUPIED && chunkcoord_equal(slot->coord, coord)) {
      return &slot->block;
    }
    i = (i + 1) % w->capacity;
  }
  return NULL; // probed every slot without finding it (table full of other
               // entries, none matching) -- shouldn't happen given
               // world_insert's load check, but no infinite loop either way
}

bool world_insert(World *w, ChunkCoord coord, Mapblock block) {
  if (w->count >= w->capacity) {
    // Hard-fail, don't rehash-and-grow: capacity is sized once up front for
    // the largest view distance you intend to support. Running out means
    // that budget was wrong, not that this function should silently cover
    // for it.
    fprintf(stderr,
            "world_insert: table full (capacity %u) -- raise capacity, "
            "don't grow silently\n",
            w->capacity);
    exit(1);
  }
  uint32_t i = world_hash(coord, w->capacity);
  for (;;) {
    WorldSlot *slot = &w->slots[i];
    if (slot->state == SLOT_EMPTY) {
      slot->state = SLOT_OCCUPIED;
      slot->coord = coord;
      slot->block = block;
      w->count++;
      return true;
    }
    if (slot->state == SLOT_OCCUPIED && chunkcoord_equal(slot->coord, coord)) {
      // Overwrite: insert doubles as update for an already-loaded chunk
      // (e.g. re-decoding a mapblock the server resent). The old block's
      // heap allocation (if MAPBLOCK_FULL) is freed first so this doesn't
      // leak -- if you wanted "caller bug" semantics instead, this is
      // where you'd assert/hard-fail instead of destroying+replacing.
      mapblock_destroy(&slot->block);
      slot->block = block;
      return true;
    }
    i = (i + 1) % w->capacity;
    // No bounds check against a probe count here: the w->count >= capacity
    // check above guarantees at least one EMPTY slot exists before this
    // loop starts, so it's guaranteed to terminate.
  }
}
