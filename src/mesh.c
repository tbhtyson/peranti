#include "mesh.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static bool is_solid(Node n) {
  return n.content_id != CONTENT_AIR && n.content_id != CONTENT_IGNORE;
}

// Solidity of local (x,y,z) within `coord`'s own block, or false if
// `coord` isn't loaded in `w` at all.
static bool sample(World *w, ChunkCoord coord, int x, int y, int z) {
  Mapblock *mb = world_get(w, coord);
  if (!mb) {
    return false;
  }
  return is_solid(mapblock_get(mb, x, y, z));
}

void occupancy_build(World *w, ChunkCoord coord, OccupancyMasks *out) {
  // X axis: border from the x-1/x+1 neighbor, sampled at their own local
  // x=15 / x=0; columns indexed by this block's own (y, z).
  ChunkCoord xm = {coord.x - 1, coord.y, coord.z};
  ChunkCoord xp = {coord.x + 1, coord.y, coord.z};
  for (int y = 0; y < 16; y++) {
    for (int z = 0; z < 16; z++) {
      uint32_t mask = 0;
      if (sample(w, xm, 15, y, z)) mask |= 1u << 0;
      for (int x = 0; x < 16; x++) {
        if (sample(w, coord, x, y, z)) mask |= 1u << (x + 1);
      }
      if (sample(w, xp, 0, y, z)) mask |= 1u << 17;
      out->cols[0][y][z] = mask;
    }
  }

  // Y axis: border from y-1/y+1 neighbor; columns indexed by (x, z).
  ChunkCoord ym = {coord.x, coord.y - 1, coord.z};
  ChunkCoord yp = {coord.x, coord.y + 1, coord.z};
  for (int x = 0; x < 16; x++) {
    for (int z = 0; z < 16; z++) {
      uint32_t mask = 0;
      if (sample(w, ym, x, 15, z)) mask |= 1u << 0;
      for (int y = 0; y < 16; y++) {
        if (sample(w, coord, x, y, z)) mask |= 1u << (y + 1);
      }
      if (sample(w, yp, x, 0, z)) mask |= 1u << 17;
      out->cols[1][x][z] = mask;
    }
  }

  // Z axis: border from z-1/z+1 neighbor; columns indexed by (x, y).
  ChunkCoord zm = {coord.x, coord.y, coord.z - 1};
  ChunkCoord zp = {coord.x, coord.y, coord.z + 1};
  for (int x = 0; x < 16; x++) {
    for (int y = 0; y < 16; y++) {
      uint32_t mask = 0;
      if (sample(w, zm, x, y, 15)) mask |= 1u << 0;
      for (int z = 0; z < 16; z++) {
        if (sample(w, coord, x, y, z)) mask |= 1u << (z + 1);
      }
      if (sample(w, zp, x, y, 0)) mask |= 1u << 17;
      out->cols[2][x][y] = mask;
    }
  }
}

// Restricts a column mask (bits 0..17) to just the 16 bits that map to
// this block's own local nodes (bits 1..16) -- bits 0 and 17 are the
// neighbor's border, only used below to determine whether a boundary face
// exists, never as a face position themselves.
#define INTERIOR_MASK 0x1FFFEu

// Maps (axis, a, b, n) back to the full local (x, y, z) it represents,
// using occupancy_build's own cols[axis][a][b] convention. Shared by both
// the face-grid extraction and the quad emission below so the two can't
// drift apart from each other.
static void axis_to_xyz(int axis, int a, int b, int n, int *x, int *y,
                         int *z) {
  switch (axis) {
  case 0:
    *x = n;
    *y = a;
    *z = b;
    break;
  case 1:
    *x = a;
    *y = n;
    *z = b;
    break;
  default: // 2
    *x = a;
    *y = b;
    *z = n;
    break;
  }
}

// Extracts a 16x16 (present, content_id) grid for one axis/direction/depth
// combination: present[a][b] is true if a face exists at local depth `n`
// on `direction`'s side, and content[a][b] is the content id of the solid
// node the face belongs to (undefined where present[a][b] is false).
static void build_face_grid(uint32_t occ_col[16][16], int axis,
                             int direction, int n, const Mapblock *own,
                             bool present[16][16], uint16_t content[16][16]) {
  for (int a = 0; a < 16; a++) {
    for (int b = 0; b < 16; b++) {
      uint32_t col = occ_col[a][b];
      // direction 0 (negative-facing): solid at i, non-solid at i-1.
      // direction 1 (positive-facing): solid at i, non-solid at i+1.
      uint32_t faces = direction == 0 ? (col & ~(col << 1)) & INTERIOR_MASK
                                       : (col & ~(col >> 1)) & INTERIOR_MASK;
      bool has_face = (faces & (1u << (n + 1))) != 0;
      present[a][b] = has_face;
      if (has_face) {
        int x, y, z;
        axis_to_xyz(axis, a, b, n, &x, &y, &z);
        content[a][b] = mapblock_get(own, x, y, z).content_id;
      }
    }
  }
}

// 2D greedy rectangle merge over one axis/direction/depth plane. For each
// unvisited present cell: grow a run along `a` while content keeps
// matching, then grow along `b` while every cell in the current a-range
// still matches at that b -- the standard "grow one axis, then grow the
// other only while the whole strip agrees" greedy-meshing merge, not the
// (harder, unnecessary at 16x16) fully general maximal-rectangle solve.
static void merge_plane(bool present[16][16], uint16_t content[16][16],
                         int axis, int direction, int n, Mesh *out) {
  bool visited[16][16] = {0};
  for (int a = 0; a < 16; a++) {
    for (int b = 0; b < 16; b++) {
      if (visited[a][b] || !present[a][b]) {
        continue;
      }
      uint16_t id = content[a][b];

      int height = 1; // extent along `a`
      while (a + height < 16 && !visited[a + height][b] &&
             present[a + height][b] && content[a + height][b] == id) {
        height++;
      }

      int width = 1; // extent along `b`
      while (b + width < 16) {
        bool strip_matches = true;
        for (int da = 0; da < height; da++) {
          if (visited[a + da][b + width] || !present[a + da][b + width] ||
              content[a + da][b + width] != id) {
            strip_matches = false;
            break;
          }
        }
        if (!strip_matches) {
          break;
        }
        width++;
      }

      for (int da = 0; da < height; da++) {
        for (int db = 0; db < width; db++) {
          visited[a + da][b + db] = true;
        }
      }

      if (out->count >= out->capacity) {
        fprintf(stderr, "merge_plane: quad output exceeded capacity %u\n",
                out->capacity);
        exit(1);
      }
      int x, y, z;
      axis_to_xyz(axis, a, b, n, &x, &y, &z);
      out->quads[out->count++] = (Quad){
          .x = (uint8_t)x,
          .y = (uint8_t)y,
          .z = (uint8_t)z,
          .width = (uint8_t)width,
          .height = (uint8_t)height,
          .axis = (uint8_t)axis,
          .direction = (uint8_t)direction,
          .content_id = id,
      };
    }
  }
}

void mesh_init(Mesh *m) {
  m->quads = malloc(sizeof(Quad) * MESH_MAX_QUADS);
  if (!m->quads) {
    fprintf(stderr, "mesh_init: out of memory\n");
    exit(1);
  }
  m->capacity = MESH_MAX_QUADS;
  m->count = 0;
}

void mesh_destroy(Mesh *m) {
  free(m->quads);
  m->quads = NULL;
  m->capacity = 0;
  m->count = 0;
}

void mesh_build(World *w, ChunkCoord coord, Mesh *out) {
  out->count = 0;
  Mapblock *own = world_get(w, coord);
  if (!own) {
    return; // nothing loaded here, nothing to mesh
  }

  OccupancyMasks occ;
  occupancy_build(w, coord, &occ);

  bool present[16][16];
  uint16_t content[16][16];
  for (int axis = 0; axis < 3; axis++) {
    for (int direction = 0; direction < 2; direction++) {
      for (int n = 0; n < 16; n++) {
        build_face_grid(occ.cols[axis], axis, direction, n, own, present,
                         content);
        merge_plane(present, content, axis, direction, n, out);
      }
    }
  }
}
