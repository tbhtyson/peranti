#include "mesh_legacy.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void cross(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1]*b[2] - a[2]*b[1];
  out[1] = a[2]*b[0] - a[0]*b[2];
  out[2] = a[0]*b[1] - a[1]*b[0];
}
static void sub(const float a[3], const float b[3], float out[3]) {
  out[0]=a[0]-b[0]; out[1]=a[1]-b[1]; out[2]=a[2]-b[2];
}
static float dot(const float a[3], const float b[3]) {
  return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}

int main(void) {
  World w;
  world_init(&w, 101);
  ChunkCoord c = {0, 0, 0};
  Node stone = {.content_id = 3, .param1 = 0, .param2 = 0};
  world_insert(&w, c, mapblock_make_uniform(stone));

  Mesh m;
  mesh_init(&m);
  mesh_build(&w, c, &m);
  assert(m.count == 6); // one quad per axis/direction combo -- full coverage

  Vertex *verts; uint32_t vcount;
  uint16_t *indices; uint32_t icount;
  mesh_to_legacy_buffers(&m, &verts, &vcount, &indices, &icount);

  // Matches the original hardcoded cube exactly: 24 vertices, 36 indices.
  assert(vcount == 24);
  assert(icount == 36);
  printf("vertex/index counts match the original hardcoded cube (24/36)\n");

  // Winding check: for every triangle, cross(v1-v0, v2-v0) must point the
  // same way as the shared vertex normal (right-hand rule for a
  // CCW-as-seen-from-the-normal-side triangle). Checked across all 12
  // triangles -- both directions of all 3 axes are covered since the
  // synthetic scene produced exactly one quad per combination.
  for (uint32_t t = 0; t < icount / 3; t++) {
    uint16_t i0 = indices[t*3+0], i1 = indices[t*3+1], i2 = indices[t*3+2];
    float e1[3], e2[3], n[3];
    sub(verts[i1].pos, verts[i0].pos, e1);
    sub(verts[i2].pos, verts[i0].pos, e2);
    cross(e1, e2, n);
    float d = dot(n, verts[i0].normal);
    if (d <= 0.0f) {
      printf("BAD WINDING: triangle %u, dot=%f (indices %u,%u,%u)\n", t, (double)d, i0, i1, i2);
    }
    assert(d > 0.0f);
  }
  printf("all 12 triangles wind correctly relative to their normals\n");

  free(verts);
  free(indices);
  mesh_destroy(&m);
  world_destroy(&w);
  printf("ALL MESH_LEGACY TESTS PASSED\n");
  return 0;
}
