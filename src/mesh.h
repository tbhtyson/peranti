#ifndef PERANTI_MESH_H
#define PERANTI_MESH_H

#include "webgpu-headers/webgpu.h"
#include <stddef.h>

typedef struct {
    float x, y;
    float r, g, b;
} Vertex;

typedef struct {
    WGPUBuffer buffer;
    size_t vertex_count;
} Mesh;

Mesh mesh_create(WGPUDevice device, WGPUQueue queue, const Vertex *vertices, size_t vertex_count);
void mesh_destroy(Mesh *mesh);

#endif // PERANTI_MESH_H
