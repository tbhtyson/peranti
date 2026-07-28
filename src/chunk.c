#include "chunk.h"
#include "block.h"
block_id_t chunk_get_block(const Chunk *chunk, int x, int y, int z) {
  return chunk->blocks[chunk_block_index(x, y, z)];
}
void chunk_set_block(Chunk *chunk, int x, int y, int z, block_id_t id) {
  chunk->blocks[chunk_block_index(x, y, z)] = id;
}
