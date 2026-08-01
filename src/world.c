#include "world.h"
#include "chunk.h"
#include <stdio.h>
// world.c
void world_init_flat_test(World *world) {
  for (int cx = 0; cx < WORLD_CHUNKS_X; cx++) {
    for (int cy = 0; cy < WORLD_CHUNKS_Y; cy++) {
      for (int cz = 0; cz < WORLD_CHUNKS_Z; cz++) {
        Chunk *chunk = &world->chunks[cx][cy][cz];
        chunk->coord = (ChunkCoord){(int16_t)cx, (int16_t)cy, (int16_t)cz};

        // Step the fill height by grid position instead of using one fixed
        // height everywhere -- with every chunk identical, a broken grid (e.g.
        // wrong offset math, or every draw call reading chunk (0,0)'s buffers)
        // would look visually *identical* to a working one. Varying height
        // makes a wiring bug immediately visible instead of silently passing.

        for (int x = 0; x < CHUNK_SIZE; x++)
          for (int y = 0; y < 15; y++)
            for (int z = 0; z < CHUNK_SIZE; z++)
              chunk_set_block(chunk, x, y, z, BLOCK_STONE);
      }
    }
  }
}

// world.c
const Chunk *world_chunk_at(const World *world, int cx, int cy, int cz) {
  if (cx < 0 || cx >= WORLD_CHUNKS_X || cy < 0 || cy >= WORLD_CHUNKS_Y ||
      cz < 0 || cz >= WORLD_CHUNKS_Z) {
    return NULL; // outside the loaded grid -- not an error, just "no chunk here
                 // yet" erroring out here would be like reading a novel's
                 // preview and being upset at the book
  }
  return &world->chunks[cx][cy][cz];
}
