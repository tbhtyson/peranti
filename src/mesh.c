#include "mesh.h"

const vertex_t cube_vertices[24] = {
    // +X face (red)
    {0.5f, -0.5f, -0.5f, 1, 0, 0, 1},
    {0.5f, 0.5f, -0.5f, 1, 0, 0, 1},
    {0.5f, 0.5f, 0.5f, 1, 0, 0, 1},
    {0.5f, -0.5f, 0.5f, 1, 0, 0, 1},
    // -X face (green)
    {-0.5f, -0.5f, 0.5f, 0, 1, 0, 1},
    {-0.5f, 0.5f, 0.5f, 0, 1, 0, 1},
    {-0.5f, 0.5f, -0.5f, 0, 1, 0, 1},
    {-0.5f, -0.5f, -0.5f, 0, 1, 0, 1},
    // +Y face (blue)
    {-0.5f, 0.5f, -0.5f, 0, 0, 1, 1},
    {-0.5f, 0.5f, 0.5f, 0, 0, 1, 1},
    {0.5f, 0.5f, 0.5f, 0, 0, 1, 1},
    {0.5f, 0.5f, -0.5f, 0, 0, 1, 1},
    // -Y face (yellow)
    {-0.5f, -0.5f, 0.5f, 1, 1, 0, 1},
    {-0.5f, -0.5f, -0.5f, 1, 1, 0, 1},
    {0.5f, -0.5f, -0.5f, 1, 1, 0, 1},
    {0.5f, -0.5f, 0.5f, 1, 1, 0, 1},
    // +Z face (magenta)
    {-0.5f, -0.5f, 0.5f, 1, 0, 1, 1},
    {0.5f, -0.5f, 0.5f, 1, 0, 1, 1},
    {0.5f, 0.5f, 0.5f, 1, 0, 1, 1},
    {-0.5f, 0.5f, 0.5f, 1, 0, 1, 1},
    // -Z face (cyan)
    {0.5f, -0.5f, -0.5f, 0, 1, 1, 1},
    {-0.5f, -0.5f, -0.5f, 0, 1, 1, 1},
    {-0.5f, 0.5f, -0.5f, 0, 1, 1, 1},
    {0.5f, 0.5f, -0.5f, 0, 1, 1, 1},
};

const uint16_t cube_indices[36] = {
    0,  1,  2,  0,  2,  3,  // +X
    4,  5,  6,  4,  6,  7,  // -X
    8,  9,  10, 8,  10, 11, // +Y
    12, 13, 14, 12, 14, 15, // -Y
    16, 17, 18, 16, 18, 19, // +Z
    20, 21, 22, 20, 22, 23, // -Z
};
// mesh.c
sg_bindings mesh_cube_create(void) {
  sg_bindings bind = {0};
  bind.vertex_buffers[0] = sg_make_buffer(&(sg_buffer_desc){
      .data = SG_RANGE(cube_vertices),
      // .usage defaults to { .vertex_buffer = true, .immutable = true }, so
      // this line can stay as-is
  });
  bind.index_buffer = sg_make_buffer(&(sg_buffer_desc){
      .usage = {.index_buffer = true, .immutable = true},
      .data = SG_RANGE(cube_indices),
  });
  return bind;
}
