#ifndef PERANTI_MESH_H
#define PERANTI_MESH_H

#include <stdint.h>
#include "sokol_gfx.h"

typedef struct {
    float x, y, z;
    float r, g, b, a;
} vertex_t;

extern const vertex_t cube_vertices[24];
extern const uint16_t cube_indices[36];
sg_bindings mesh_cube_create(void);

#endif
