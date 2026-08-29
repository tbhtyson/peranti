#include "chunk.h"

#include <string.h>

// Same guard as chunk.h, and for the same reason: if this file gets
// #include'd raw into a C++ translation unit (the graft amalgamation),
// chunk.h's own extern "C" block closes before these function *definitions*
// appear, so without this they'd get C++ linkage while the header declared
// them with C linkage -- a mismatch, not just a style nit. Self-containing
// this means nothing #including this file ever has to remember to wrap it.
#ifdef __cplusplus
extern "C" {
#endif

void chunk_init(Chunk *chunk) {
  memset(chunk->blocks, CONTENT_AIR, sizeof(chunk->blocks));
}

ContentType chunk_get(const Chunk *chunk, int x, int y, int z) {
  return (ContentType)chunk->blocks[x][y][z];
}

void chunk_set(Chunk *chunk, int x, int y, int z, ContentType value) {
  chunk->blocks[x][y][z] = (uint8_t)value;
}

#ifdef __cplusplus
}
#endif
