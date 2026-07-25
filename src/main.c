#include "mesh.h"
#include "pipeline.h"
#include "surface.h"
#include "webgpu-headers/webgpu.h"
#include "webgpu_context.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

const uint32_t WINDOW_WIDTH = 1600;
const uint32_t WINDOW_HEIGHT = 900;
uint32_t windowWidth = WINDOW_WIDTH;
uint32_t windowHeight = WINDOW_HEIGHT;

int main(void) {
  float ratio = (float)windowWidth/windowHeight;
  printf("Aspect Ratio: %f\n", ratio);
  WebGpuContext ctx =
      webgpu_context_create(WINDOW_WIDTH, WINDOW_HEIGHT, "Peranti");
  WGPURenderPipeline pipeline =
      pipeline_create_triangle(ctx.device, ctx.surface_format);

  static const Vertex triangle_vertices[] = {
    {0.0f, 0.5f,  1.0f, 0.0f, 0.0f},   // red
    {-0.5f, -0.5f, 0.0f, 1.0f, 0.0f},  // green
    {0.5f, -0.5f, 0.0f, 0.0f, 1.0f},    // blue

};
  Mesh triangle_mesh =
      mesh_create(ctx.device, ctx.queue, triangle_vertices,
                  sizeof(triangle_vertices) / sizeof(triangle_vertices[0]));

  while (!glfwWindowShouldClose(ctx.window)) {
    glfwPollEvents();

    WGPUSurfaceTexture surface_texture = {0};
    wgpuSurfaceGetCurrentTexture(ctx.surface, &surface_texture);

    if (surface_texture.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surface_texture.status !=
            WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
      fprintf(stderr, "Failed to acquire surface texture (status %d)\n",
              surface_texture.status);
      // for now: bail. Later: handle Timeout/Outdated by reconfiguring the
      // surface and skipping this frame.
      exit(1);
    }

    WGPUTextureView view = wgpuTextureCreateView(surface_texture.texture, NULL);
    WGPUCommandEncoder encoder =
        wgpuDeviceCreateCommandEncoder(ctx.device, NULL);

    WGPURenderPassColorAttachment color_attachment = {0};
    color_attachment.nextInChain = NULL;
    color_attachment.view = view;
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_attachment.resolveTarget = NULL;
    color_attachment.loadOp = WGPULoadOp_Clear;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.clearValue = (WGPUColor){0.5, 0.8, 0.9, 1.0};

    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    WGPURenderPassEncoder pass =
        wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);

    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, triangle_mesh.buffer, 0,
                                         triangle_mesh.vertex_count *
                                             sizeof(Vertex));
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, (uint32_t)triangle_mesh.vertex_count, 1, 0,
                              0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmd_buffer_desc = {0};
    WGPUCommandBuffer cmd_buffer =
        wgpuCommandEncoderFinish(encoder, &cmd_buffer_desc);
    wgpuCommandEncoderRelease(encoder);

    wgpuQueueSubmit(ctx.queue, 1, &cmd_buffer);
    wgpuCommandBufferRelease(cmd_buffer);
    wgpuSurfacePresent(ctx.surface);
    wgpuTextureViewRelease(view);
  }

  mesh_destroy(&triangle_mesh);
  wgpuRenderPipelineRelease(pipeline);
  webgpu_context_destroy(&ctx);
  return 0;
}
