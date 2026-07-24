#include "mesh.h"
#include <stdio.h>
#include <stdlib.h>

Mesh mesh_create(WGPUDevice device, WGPUQueue queue, const Vertex *vertices,
                 size_t vertex_count) {
  // build WGPUBufferDescriptor, wgpuDeviceCreateBuffer, wgpuQueueWriteBuffer
  // hard-fail if buffer creation returns NULL
  // return (Mesh){ .buffer = ..., .vertex_count = vertex_count };
  uint64_t byte_size = (uint64_t)(vertex_count * sizeof(Vertex));

  WGPUBufferDescriptor buffer_desc = {0};
  buffer_desc.nextInChain = NULL;
  buffer_desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
  buffer_desc.size = byte_size;
  buffer_desc.mappedAtCreation = WGPU_FALSE;

  WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &buffer_desc);
  if (!buffer) {
    fprintf(stderr, "Failed to create vertex buffer\n");
    exit(1);
  }

  wgpuQueueWriteBuffer(queue, buffer, 0, vertices, (size_t)byte_size);

  return (Mesh){.buffer = buffer, .vertex_count = vertex_count};
}

void mesh_destroy(Mesh *mesh) { wgpuBufferRelease(mesh->buffer); }
