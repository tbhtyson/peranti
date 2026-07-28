#ifndef PERANTI_CHUNK_MESH_H
#define PERANTI_CHUNK_MESH_H

#include "chunk.h"
#include "mesh.h"  // reuses your existing vertex_t

typedef struct {
    vertex_t *vertices;
    uint32_t  vertex_count;
    uint32_t *indices;   // uint32_t, not uint16_t -- see note below
    uint32_t  index_count;
} ChunkMeshData;

// Builds a naive per-visible-face mesh (one quad per exposed block face,
// no greedy merging yet). Neighbor chunks are passed in so faces at the
// chunk boundary cull correctly against blocks in the adjacent chunk --
// pass NULL for a neighbor that isn't loaded yet; treat NULL as "opaque"
// so you don't get see-through gaps at the edge of loaded terrain while
// chunks are still streaming in.
ChunkMeshData chunk_mesh_build_naive(
    const Chunk *chunk,
    const Chunk *neighbor_pos_x, const Chunk *neighbor_neg_x,
    const Chunk *neighbor_pos_y, const Chunk *neighbor_neg_y,
    const Chunk *neighbor_pos_z, const Chunk *neighbor_neg_z
);

void chunk_mesh_free(ChunkMeshData *mesh);

#endif
