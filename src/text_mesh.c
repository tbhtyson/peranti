#include "text_mesh.h"
#include "segdisplay.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STROKE_WIDTH_FRACTION 0.12f // fraction of a cell's width

static const GlyphEntry *find_glyph(char c) {
  for (size_t i = 0; i < glyph_table_count; i++) {
    if (glyph_table[i].character == c) return &glyph_table[i];
  }
  return NULL; // unrecognized char (space, ':', etc.) -- rendered blank
}

static void pixel_to_ndc(float px, float py, uint32_t window_width,
                         uint32_t window_height, float *ndc_x, float *ndc_y) {
  *ndc_x = (px / (float)window_width) * 2.0f - 1.0f;
  *ndc_y = 1.0f - (py / (float)window_height) * 2.0f; // pixel y grows down, NDC y grows up
}

// Appends one stroke quad (two triangles, four verts) between two pixel
// points into the output arrays, advancing both cursors.
static void append_segment_quad(Vertex2D *vertices, uint16_t *indices,
                                size_t *vertex_cursor, size_t *index_cursor,
                                float ax, float ay, float bx, float by,
                                float stroke_width_px, float r, float g, float b,
                                uint32_t window_width, uint32_t window_height) {
  float dx = bx - ax;
  float dy = by - ay;
  float len = sqrtf(dx * dx + dy * dy); // segment endpoints are always distinct -- no zero-length case

  float perp_x = -dy / len * (stroke_width_px * 0.5f);
  float perp_y = dx / len * (stroke_width_px * 0.5f);

  float corners_px[4][2] = {
      {ax + perp_x, ay + perp_y},
      {bx + perp_x, by + perp_y},
      {bx - perp_x, by - perp_y},
      {ax - perp_x, ay - perp_y},
  };

  uint16_t base_index = (uint16_t)*vertex_cursor;
  for (int i = 0; i < 4; i++) {
    float ndc_x, ndc_y;
    pixel_to_ndc(corners_px[i][0], corners_px[i][1], window_width, window_height,
                &ndc_x, &ndc_y);
    vertices[*vertex_cursor] = (Vertex2D){ndc_x, ndc_y, r, g, b};
    (*vertex_cursor)++;
  }

  indices[*index_cursor + 0] = (uint16_t)(base_index + 0);
  indices[*index_cursor + 1] = (uint16_t)(base_index + 1);
  indices[*index_cursor + 2] = (uint16_t)(base_index + 2);
  indices[*index_cursor + 3] = (uint16_t)(base_index + 0);
  indices[*index_cursor + 4] = (uint16_t)(base_index + 2);
  indices[*index_cursor + 5] = (uint16_t)(base_index + 3);
  *index_cursor += 6;
}

TextGeometry text_mesh_build(const char *text, float origin_x, float origin_y,
                             float scale, uint32_t window_width, uint32_t window_height) {
  size_t text_length = strlen(text);

  // worst case: every character lights all 16 segments
  size_t max_vertices = text_length * 16 * 4;
  size_t max_indices = text_length * 16 * 6;

  Vertex2D *vertices = malloc(max_vertices * sizeof(Vertex2D));
  uint16_t *indices = malloc(max_indices * sizeof(uint16_t));
  if (!vertices || !indices) {
    fprintf(stderr, "Failed to allocate text mesh geometry\n");
    exit(1);
  }

  size_t vertex_cursor = 0;
  size_t index_cursor = 0;

  float cell_width_px = scale;
  float cell_height_px = scale * 2.0f;
  float advance_px = cell_width_px * 1.3f; // gap between character cells
  float stroke_width_px = scale * STROKE_WIDTH_FRACTION;

  for (size_t i = 0; i < text_length; i++) {
    const GlyphEntry *glyph = find_glyph(text[i]);
    if (!glyph) continue; // blank -- still advances via the loop's char_origin_x below

    float char_origin_x = origin_x + (float)i * advance_px;

    for (int seg = 0; seg < 16; seg++) {
      if (!(glyph->segments & (1u << seg))) continue;

      uint8_t node_a = segment_endpoints[seg][0];
      uint8_t node_b = segment_endpoints[seg][1];

      float ax = char_origin_x + segment_nodes[node_a].x * cell_width_px;
      float ay = origin_y + (2.0f - segment_nodes[node_a].y) * (cell_height_px * 0.5f);
      float bx = char_origin_x + segment_nodes[node_b].x * cell_width_px;
      float by = origin_y + (2.0f - segment_nodes[node_b].y) * (cell_height_px * 0.5f);

      append_segment_quad(vertices, indices, &vertex_cursor, &index_cursor,
                          ax, ay, bx, by, stroke_width_px,
                          1.0f, 1.0f, 1.0f, // white
                          window_width, window_height);
    }
  }

  return (TextGeometry){
      .vertices = vertices,
      .indices = indices,
      .vertex_count = vertex_cursor,
      .index_count = index_cursor,
  };
}
// in text_mesh.c
Mesh text_mesh_create(WGPUDevice device, WGPUQueue queue,
                      const Vertex2D *vertices, size_t vertex_count,
                      const uint16_t *indices, size_t index_count) {
  uint64_t vertex_byte_size = (uint64_t)(vertex_count * sizeof(Vertex2D));

  WGPUBufferDescriptor vertex_buffer_desc = {0};
  vertex_buffer_desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
  vertex_buffer_desc.size = vertex_byte_size;
  vertex_buffer_desc.mappedAtCreation = WGPU_FALSE;

  WGPUBuffer vertex_buffer = wgpuDeviceCreateBuffer(device, &vertex_buffer_desc);
  if (!vertex_buffer) {
    fprintf(stderr, "Failed to create text vertex buffer\n");
    exit(1);
  }
  wgpuQueueWriteBuffer(queue, vertex_buffer, 0, vertices, (size_t)vertex_byte_size);

  uint64_t index_byte_size = (uint64_t)(index_count * sizeof(uint16_t));

  WGPUBufferDescriptor index_buffer_desc = {0};
  index_buffer_desc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
  index_buffer_desc.size = index_byte_size;
  index_buffer_desc.mappedAtCreation = WGPU_FALSE;

  WGPUBuffer index_buffer = wgpuDeviceCreateBuffer(device, &index_buffer_desc);
  if (!index_buffer) {
    fprintf(stderr, "Failed to create text index buffer\n");
    exit(1);
  }
  wgpuQueueWriteBuffer(queue, index_buffer, 0, indices, (size_t)index_byte_size);

  return (Mesh){.vertex_buffer = vertex_buffer,
                .index_buffer = index_buffer,
                .vertex_count = vertex_count,
                .index_count = index_count};
}
