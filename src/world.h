// world.h
#ifndef PERANTI_WORLD_H
#define PERANTI_WORLD_H

#include "chunk.h"

#define WORLD_CHUNKS_X 8
#define WORLD_CHUNKS_Y 8
#define WORLD_CHUNKS_Z 8

typedef struct {
    Chunk chunks[WORLD_CHUNKS_X][WORLD_CHUNKS_Y][WORLD_CHUNKS_Z];
} World;

void world_init_flat_test(World *world); // fills every chunk, same pattern as your test_chunk

// Returns NULL if (cx, cz) is outside the grid -- caller (chunk_mesh_build_naive
// call site) already treats NULL as "no neighbor, treat as opaque," so this
// composes with what you have for free.
const Chunk *world_chunk_at(const World *world, int cx, int cy, int cz);

#endif
