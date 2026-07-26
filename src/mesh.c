#include "mesh.h"
#include <stdio.h>
#include <stdlib.h>

Mesh mesh_create(WGPUDevice device, WGPUQueue queue,
                 const Vertex *vertices, size_t vertex_count,
                 const uint16_t *indices, size_t index_count) {
  uint64_t vertex_byte_size = (uint64_t)(vertex_count * sizeof(Vertex));

  WGPUBufferDescriptor vertex_buffer_desc = {0};
  vertex_buffer_desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
  vertex_buffer_desc.size = vertex_byte_size;
  vertex_buffer_desc.mappedAtCreation = WGPU_FALSE;

  WGPUBuffer vertex_buffer = wgpuDeviceCreateBuffer(device, &vertex_buffer_desc);
  if (!vertex_buffer) {
    fprintf(stderr, "Failed to create vertex buffer\n");
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
    fprintf(stderr, "Failed to create index buffer\n");
    exit(1);
  }
  wgpuQueueWriteBuffer(queue, index_buffer, 0, indices, (size_t)index_byte_size);

  return (Mesh){.vertex_buffer = vertex_buffer,
                .index_buffer = index_buffer,
                .vertex_count = vertex_count,
                .index_count = index_count};
}

void mesh_destroy(Mesh *mesh) {
  wgpuBufferRelease(mesh->vertex_buffer);
  wgpuBufferRelease(mesh->index_buffer);
}

/* REMEMBER: wgpuQueueWriteBuffer requires 4-byte multiple sized data, a.k.a. even index_count*/
