#include "chunk_mesh.h"
#include "block.h"
#include "chunk.h"
#include <stdio.h>
#include <stdlib.h>

// chunk_mesh.c

typedef struct {
  int8_t dx, dy, dz;
} CornerOffset;

// Corner order per face, verified against cube_vertices' existing winding
// (each face here traces the same 4-corner path your cube used).
static const CornerOffset face_corners[6][4] = {
    /* +X */ {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}},
    /* -X */ {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}},
    /* +Y */ {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}},
    /* -Y */ {{0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}},
    /* +Z */ {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}},
    /* -Z */ {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},
};

static const struct {
  float r, g, b;
} face_color[6] = {
    /* +X */ {1.0f, 0.0f, 0.0f}, // red
    /* -X */ {0.0f, 1.0f, 0.0f}, // green
    /* +Y */ {0.0f, 0.0f, 1.0f}, // blue
    /* -Y */ {1.0f, 1.0f, 0.0f}, // yellow
    /* +Z */ {1.0f, 0.0f, 1.0f}, // magenta
    /* -Z */ {0.0f, 1.0f, 1.0f}, // cyan
};

static const int8_t face_normal[6][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

// neighbors[] indexed the same as face_normal/face_corners (+X,-X,+Y,-Y,+Z,-Z)
static bool is_face_visible(const Chunk *chunk, const Chunk *neighbors[6],
                            int x, int y, int z, int face) {
  int nx = x + face_normal[face][0];
  int ny = y + face_normal[face][1];
  int nz = z + face_normal[face][2];

  if (nx >= 0 && nx < CHUNK_SIZE && ny >= 0 && ny < CHUNK_SIZE && nz >= 0 &&
      nz < CHUNK_SIZE) {
    return block_is_air_like(chunk_get_block(chunk, nx, ny, nz));
  }

  const Chunk *neighbor = neighbors[face];
  if (!neighbor)
    return true; // no neighbor loaded -- for an isolated test chunk, treat as
                 // exposed

  int wx = (nx + CHUNK_SIZE) % CHUNK_SIZE;
  int wy = (ny + CHUNK_SIZE) % CHUNK_SIZE;
  int wz = (nz + CHUNK_SIZE) % CHUNK_SIZE;
  return block_is_air_like(chunk_get_block(neighbor, wx, wy, wz));
}

static void emit_face(ChunkMeshData *mesh, int x, int y, int z, int face) {
  uint32_t base = mesh->vertex_count;

  for (int c = 0; c < 4; c++) {
    CornerOffset off = face_corners[face][c];
    mesh->vertices[mesh->vertex_count++] = (vertex_t){
        .x = (float)(x + off.dx),
        .y = (float)(y + off.dy),
        .z = (float)(z + off.dz),
        // placeholder flat white -- replace with a block_id_t -> color
        // (or UV) lookup once you're past naive meshing
        /*.r = 1.0f,
        .g = 1.0f,
        .b = 1.0f,
        .a = 1.0f,*/
        .r = face_color[face].r,
        .g = face_color[face].g,
        .b = face_color[face].b,
        .a = 1.0f,
    };
  }

  mesh->indices[mesh->index_count++] = base + 0;
  mesh->indices[mesh->index_count++] = base + 1;
  mesh->indices[mesh->index_count++] = base + 2;
  mesh->indices[mesh->index_count++] = base + 0;
  mesh->indices[mesh->index_count++] = base + 2;
  mesh->indices[mesh->index_count++] = base + 3;
}

ChunkMeshData
chunk_mesh_build_naive(const Chunk *chunk, const Chunk *neighbor_pos_x,
                       const Chunk *neighbor_neg_x, const Chunk *neighbor_pos_y,
                       const Chunk *neighbor_neg_y, const Chunk *neighbor_pos_z,
                       const Chunk *neighbor_neg_z) {
  const Chunk *neighbors[6] = {neighbor_pos_x, neighbor_neg_x, neighbor_pos_y,
                               neighbor_neg_y, neighbor_pos_z, neighbor_neg_z};
  ChunkMeshData mesh = {0};
  mesh.vertices = malloc(
      98304 * sizeof(vertex_t)); // 98304 is 4096 cubes * 6 faces * 4 vertices
  mesh.indices = malloc(
      147456 * sizeof(uint32_t)); // 147456 is 4096 cubes * 6 faces * 6 indices
  if (!mesh.vertices || !mesh.indices) {
    fprintf(stderr, "chunk_mesh_build_naive: allocation failed\n");
    exit(1);
  }
  // walk chunk, write into mesh.vertices/indices, bump
  // mesh.vertex_count/index_count
  for (int x = 0; x < 16; x++) {
    for (int y = 0; y < 16; y++) {
      for (int z = 0; z < 16; z++) {
        block_id_t id = chunk_get_block(chunk, x, y, z);
        if (!block_is_air_like(id)) {
          for (int face = 0; face < 6; face++) {
            if (is_face_visible(chunk, neighbors, x, y, z, face)) {
              emit_face(&mesh, x, y, z, face);
            }
          }
        }
      }
    }
  }
  return mesh;
}

void chunk_mesh_free(ChunkMeshData *mesh) {
  free(mesh->vertices);
  free(mesh->indices);
  mesh->vertices = NULL;
  mesh->indices = NULL;
  mesh->vertex_count = 0;
  mesh->index_count = 0;
}
