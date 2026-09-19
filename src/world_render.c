#include "world_render.h"
#include "app.h"
#include "mesh.h"
#include "mesh_legacy.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void world_render_rebuild(void) {
  /* Growable combined arrays -- doubling reallocation. Each chunk's
   * temporary legacy buffers (from mesh_to_legacy_buffers) get copied in
   * and freed immediately, so at most one chunk's temp buffers plus the
   * whole combined arrays are ever live at once -- no need to keep every
   * chunk's data around simultaneously just to learn the final size. */
  uint32_t vertexCap = 4096, vertexCount = 0;
  uint32_t indexCap = 4096, indexCount = 0;
  Vertex *vertices = malloc(sizeof(Vertex) * vertexCap);
  uint32_t *indices = malloc(sizeof(uint32_t) * indexCap);
  if (!vertices || !indices) {
    fprintf(stderr, "world_render_rebuild: OOM allocating initial combined buffers\n");
    exit(1);
  }

  for (uint32_t i = 0; i < world.capacity; i++) {
    if (world.slots[i].state != SLOT_OCCUPIED)
      continue;
    ChunkCoord coord = world.slots[i].coord;

    Mesh mesh;
    mesh_init(&mesh);
    mesh_build(&world, coord, &mesh);

    Vertex *chunkVerts;
    uint32_t chunkVertexCount;
    uint32_t *chunkIdx;
    uint32_t chunkIndexCount;
    mesh_to_legacy_buffers(&mesh, &chunkVerts, &chunkVertexCount, &chunkIdx, &chunkIndexCount);
    mesh_destroy(&mesh);

    if (chunkVertexCount == 0) {
      /* Fully air, or otherwise nothing solid to draw -- still gets
       * zero-length mallocs from mesh_to_legacy_buffers per its own
       * contract; free them and move on. */
      free(chunkVerts);
      free(chunkIdx);
      continue;
    }

    /* mesh_build()'s Quads (and mesh_to_legacy_buffers' resulting
     * Vertex.pos) are in LOCAL block-space, 0..16 per axis -- mesh.c
     * never bakes in `coord`'s absolute position, that's the caller's
     * job. Skipping this is exactly what produced "every chunk fused
     * into one solid cube with z-fighting": every chunk's geometry was
     * landing at the same local 0..16 origin regardless of which real
     * mapblock it came from, all overlapping in world space. 16 is the
     * mapblock edge length in nodes (matches mesh.c/chunk.h's own use of
     * the literal, no named constant exists for it in this codebase). */
    for (uint32_t j = 0; j < chunkVertexCount; j++) {
      chunkVerts[j].pos[0] += (float)(coord.x * 16);
      chunkVerts[j].pos[1] += (float)(coord.y * 16);
      chunkVerts[j].pos[2] += (float)(coord.z * 16);
    }

    /* This used to hard-fail here once the COMBINED vertex count across
     * every loaded chunk exceeded UINT16_MAX -- and it did, in practice:
     * 88 loaded chunks alone produced 65300 vertices, right at that wall.
     * Now that mesh_legacy.h/loop.c use 32-bit indices, that ceiling is
     * ~4 billion instead, which nothing here will realistically reach. */

    while (vertexCount + chunkVertexCount > vertexCap) {
      vertexCap *= 2;
      Vertex *grown = realloc(vertices, sizeof(Vertex) * vertexCap);
      if (!grown) {
        fprintf(stderr, "world_render_rebuild: OOM growing combined vertex buffer\n");
        exit(1);
      }
      vertices = grown;
    }
    while (indexCount + chunkIndexCount > indexCap) {
      indexCap *= 2;
      uint32_t *grown = realloc(indices, sizeof(uint32_t) * indexCap);
      if (!grown) {
        fprintf(stderr, "world_render_rebuild: OOM growing combined index buffer\n");
        exit(1);
      }
      indices = grown;
    }

    memcpy(vertices + vertexCount, chunkVerts, sizeof(Vertex) * chunkVertexCount);
    for (uint32_t j = 0; j < chunkIndexCount; j++)
      indices[indexCount + j] = chunkIdx[j] + vertexCount;

    vertexCount += chunkVertexCount;
    indexCount += chunkIndexCount;

    free(chunkVerts);
    free(chunkIdx);
  }

  if (vertexCount == 0) {
    /* Nothing loaded yet, or everything loaded is fully non-solid.
     * VkBuffer of size 0 isn't valid per the Vulkan spec, so leave the
     * previous buffer (if any) alone rather than create one -- loop.c's
     * draw call already needs an app.indexCount == 0 guard before calling
     * vkCmdDrawIndexed for exactly this case. */
    free(vertices);
    free(indices);
    return;
  }

  VkDeviceSize vBufSize = sizeof(Vertex) * vertexCount;
  VkDeviceSize iBufSize = sizeof(uint32_t) * indexCount;

  VkBufferCreateInfo bufferCI = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = vBufSize + iBufSize,
      .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT};
  VmaAllocationCreateInfo bufferAllocCI = {
      .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
               VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
               VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO};

  VkBuffer newBuffer = VK_NULL_HANDLE;
  VmaAllocation newAllocation = VK_NULL_HANDLE;
  VmaAllocationInfo newAllocationInfo = {0};
  chkvk(vmaCreateBuffer(sc.allocator, &bufferCI, &bufferAllocCI, &newBuffer, &newAllocation,
                         &newAllocationInfo));

  memcpy(newAllocationInfo.pMappedData, vertices, vBufSize);
  memcpy((char *)newAllocationInfo.pMappedData + vBufSize, indices, iBufSize);

  free(vertices);
  free(indices);

  /* The old buffer might still be bound in a command buffer the GPU
   * hasn't finished executing yet (MAX_FRAMES_IN_FLIGHT-many frames can
   * be in flight at once) -- destroying it out from under that is
   * undefined behavior per Vulkan's own synchronization rules, not just
   * a style concern. This function is meant to run infrequently (see
   * loop.c's call-site policy), so a full device-wide wait here is the
   * simple, correct choice; a deferred-deletion queue would be the right
   * answer if this ever needed to run every frame instead. NOT verified
   * against a real GPU/validation layer -- see world_render.h. */
  vkDeviceWaitIdle(app.device);

  if (app.vBuffer != VK_NULL_HANDLE)
    vmaDestroyBuffer(sc.allocator, app.vBuffer, app.vBufferAllocation);

  app.vBuffer = newBuffer;
  app.vBufferAllocation = newAllocation;
  app.vBufSize = vBufSize;
  app.indexCount = indexCount;
}
