#ifndef PERANTI_TEXT_MESH_H
#define PERANTI_TEXT_MESH_H

#include "mesh.h"
#include "webgpu.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    float x, y;
    float r, g, b;
} Vertex2D;

typedef struct {
    Vertex2D *vertices;
    uint16_t *indices;
    size_t vertex_count;
    size_t index_count;
} TextGeometry;

typedef struct {
    Mesh mesh;
    size_t vertex_capacity;
    size_t index_capacity;
} TextMeshBuffer;

// Allocates vertex/index buffers sized for `max_chars` worst-case (every
// character lighting all 16 segments). Buffers are created once and never
// resized -- text_mesh_update reuses them for the lifetime of the program.
TextMeshBuffer text_mesh_create_reserved(WGPUDevice device, WGPUQueue queue, size_t max_chars);

// Overwrites the existing buffers' contents via wgpuQueueWriteBuffer and
// updates the visible vertex/index counts. Hard-fails if vertex_count or
// index_count exceeds the capacity text_mesh_create_reserved was given.
void text_mesh_update(TextMeshBuffer *buf, WGPUQueue queue,
                     const Vertex2D *vertices, size_t vertex_count,
                     const uint16_t *indices, size_t index_count);

// Builds screen-space NDC quads for `text`. Each character cell is
// `scale` pixels wide and `scale*2` tall (matches the segment grid's
// 1x2 aspect). (origin_x, origin_y) is the pixel position of the
// top-left of the first character; window_width/window_height are
// needed to convert pixel coordinates to NDC.
// Caller owns vertices/indices and must free() both.
TextGeometry text_mesh_build(const char *text, float origin_x, float origin_y,
                             float scale, uint32_t window_width, uint32_t window_height);
// in text_mesh.h
Mesh text_mesh_create(WGPUDevice device, WGPUQueue queue,
                      const Vertex2D *vertices, size_t vertex_count,
                      const uint16_t *indices, size_t index_count);
#endif // PERANTI_TEXT_MESH_H
