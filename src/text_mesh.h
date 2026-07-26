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
