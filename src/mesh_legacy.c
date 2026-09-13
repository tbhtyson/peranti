#include "mesh_legacy.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// World-space units per node. 1.0 is an arbitrary placeholder scale, not a
// decision about Luanti's own BS=10 convention -- revisit once the
// camera-relative floating-origin coordinate system (Phase 3) exists.
#define NODE_SIZE 1.0f

// Whether axis `axis`'s (a, b) pairing (see mesh.h's cols[axis][a][b]
// convention) is a right-handed rotation of (x, y, z) or a
// handedness-reversing swap:
//   axis 0, X (a=y, b=z, main=x): (y,z,x) is a cyclic rotation of
//     (x,y,z) -- handedness-preserving.
//   axis 1, Y (a=x, b=z, main=y): (x,z,y) swaps y/z relative to any
//     cyclic rotation -- handedness-reversing.
//   axis 2, Z (a=x, b=y, main=z): (x,y,z) itself -- handedness-preserving.
// This isn't a guess -- it's confirmed against all 6 faces of the
// existing hardcoded cube's actual vertex winding, which is why Y needs
// the opposite corner-ordering rule from X and Z below.
static bool axis_is_handedness_reversing(int axis) { return axis == 1; }

static void emit_quad(const Quad *q, Vertex *verts, uint16_t *indices,
                       uint32_t vertex_base, uint32_t index_base) {
  float lowA, highA, lowB, highB, main_coord;
  float normal[3] = {0.0f, 0.0f, 0.0f};

  switch (q->axis) {
  case 0: // X: a=y, b=z
    lowA = q->y * NODE_SIZE;
    highA = (q->y + q->height) * NODE_SIZE;
    lowB = q->z * NODE_SIZE;
    highB = (q->z + q->width) * NODE_SIZE;
    main_coord = (q->x + (q->direction ? 1 : 0)) * NODE_SIZE;
    normal[0] = q->direction ? 1.0f : -1.0f;
    break;
  case 1: // Y: a=x, b=z
    lowA = q->x * NODE_SIZE;
    highA = (q->x + q->height) * NODE_SIZE;
    lowB = q->z * NODE_SIZE;
    highB = (q->z + q->width) * NODE_SIZE;
    main_coord = (q->y + (q->direction ? 1 : 0)) * NODE_SIZE;
    normal[1] = q->direction ? 1.0f : -1.0f;
    break;
  default: // Z: a=x, b=y
    lowA = q->x * NODE_SIZE;
    highA = (q->x + q->height) * NODE_SIZE;
    lowB = q->y * NODE_SIZE;
    highB = (q->y + q->width) * NODE_SIZE;
    main_coord = (q->z + (q->direction ? 1 : 0)) * NODE_SIZE;
    normal[2] = q->direction ? 1.0f : -1.0f;
    break;
  }

  // "direct" order: (lowA,lowB) (highA,lowB) (highA,highB) (lowA,highB)
  // "flipped" order: (highA,lowB) (lowA,lowB) (lowA,highB) (highA,highB)
  // Non-Y axes use direct for direction=1 (positive), flipped for
  // direction=0. Y is handedness-reversed, so it's the other way around.
  bool use_direct = axis_is_handedness_reversing(q->axis)
                        ? (q->direction == 0)
                        : (q->direction == 1);

  float corner_a[4], corner_b[4];
  if (use_direct) {
    corner_a[0] = lowA;  corner_b[0] = lowB;
    corner_a[1] = highA; corner_b[1] = lowB;
    corner_a[2] = highA; corner_b[2] = highB;
    corner_a[3] = lowA;  corner_b[3] = highB;
  } else {
    corner_a[0] = highA; corner_b[0] = lowB;
    corner_a[1] = lowA;  corner_b[1] = lowB;
    corner_a[2] = lowA;  corner_b[2] = highB;
    corner_a[3] = highA; corner_b[3] = highB;
  }

  static const float uvs[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

  for (int i = 0; i < 4; i++) {
    Vertex *v = &verts[vertex_base + (uint32_t)i];
    switch (q->axis) {
    case 0:
      v->pos[0] = main_coord;
      v->pos[1] = corner_a[i];
      v->pos[2] = corner_b[i];
      break;
    case 1:
      v->pos[0] = corner_a[i];
      v->pos[1] = main_coord;
      v->pos[2] = corner_b[i];
      break;
    default:
      v->pos[0] = corner_a[i];
      v->pos[1] = corner_b[i];
      v->pos[2] = main_coord;
      break;
    }
    v->normal[0] = normal[0];
    v->normal[1] = normal[1];
    v->normal[2] = normal[2];
    v->uv[0] = uvs[i][0];
    v->uv[1] = uvs[i][1];
  }

  static const uint16_t tri[6] = {0, 1, 2, 2, 3, 0};
  for (int i = 0; i < 6; i++) {
    indices[index_base + (uint32_t)i] = (uint16_t)(vertex_base + tri[i]);
  }
}

void mesh_to_legacy_buffers(const Mesh *mesh, Vertex **out_vertices,
                             uint32_t *out_vertex_count,
                             uint16_t **out_indices,
                             uint32_t *out_index_count) {
  uint64_t vertex_count64 = (uint64_t)mesh->count * 4;
  if (vertex_count64 > UINT16_MAX) {
    fprintf(stderr,
            "mesh_to_legacy_buffers: %u quads need %llu vertices, over the "
            "uint16 index limit (%u) -- this bridge only supports small "
            "synthetic test scenes, not real chunk-streaming scale\n",
            mesh->count, (unsigned long long)vertex_count64,
            (unsigned)UINT16_MAX);
    exit(1);
  }
  uint32_t vertex_count = (uint32_t)vertex_count64;
  uint32_t index_count = mesh->count * 6;

  Vertex *verts = malloc(sizeof(Vertex) * vertex_count);
  uint16_t *indices = malloc(sizeof(uint16_t) * index_count);
  if ((!verts && vertex_count > 0) || (!indices && index_count > 0)) {
    fprintf(stderr, "mesh_to_legacy_buffers: out of memory\n");
    exit(1);
  }

  for (uint32_t i = 0; i < mesh->count; i++) {
    emit_quad(&mesh->quads[i], verts, indices, i * 4, i * 6);
  }

  *out_vertices = verts;
  *out_vertex_count = vertex_count;
  *out_indices = indices;
  *out_index_count = index_count;
}
