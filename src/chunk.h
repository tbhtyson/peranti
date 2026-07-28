#ifndef PERANTI_CHUNK_H
#define PERANTI_CHUNK_H

#include "block.h"
#include <stdint.h>

#define CHUNK_SIZE   16
#define CHUNK_VOLUME (CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE)

typedef struct {
    int16_t x, y, z;  // chunk-space coords, NOT world/block coords
} ChunkCoord;

typedef struct {
    ChunkCoord coord;
    block_id_t blocks[CHUNK_VOLUME];
} Chunk;

// Local x/y/z must each be in [0, CHUNK_SIZE). No bounds check here --
// that's a caller-side contract (hard-fail at the caller if it's fed bad
// input), not something this hot-path indexing function should re-verify
// every call.
static inline int chunk_block_index(int x, int y, int z) {
    return x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
}

block_id_t chunk_get_block(const Chunk *chunk, int x, int y, int z);
void chunk_set_block(Chunk *chunk, int x, int y, int z, block_id_t id);

#endif
