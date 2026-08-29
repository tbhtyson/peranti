#ifndef PERANTI_CHUNK_H
#define PERANTI_CHUNK_H

// See CONVENTIONS.md rule 1: extern "C" guard lives in the header itself,
// not at the include site. Every header under src/core and src/render
// should follow this exact pattern.
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define CHUNK_SIZE 16

typedef enum {
  CONTENT_AIR = 0,
  CONTENT_STONE,
  CONTENT_DIRT,
  // ... fill in as needed
} ContentType;

typedef struct {
  uint8_t blocks[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE];
} Chunk;

void chunk_init(Chunk *chunk);
ContentType chunk_get(const Chunk *chunk, int x, int y, int z);
void chunk_set(Chunk *chunk, int x, int y, int z, ContentType value);

#ifdef __cplusplus
}
#endif

#endif // PERANTI_CHUNK_H
