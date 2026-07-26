#include "mat4.h"
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

#define PERANTI_PI 3.14159265358979323846f

int main(void) {
  float ratio = (float)windowWidth / windowHeight;
  printf("Aspect Ratio: %f\n", ratio);
  WebGpuContext ctx =
      webgpu_context_create(WINDOW_WIDTH, WINDOW_HEIGHT, "Peranti");
  PipelineWithLayout gfx =
      pipeline_create_triangle(ctx.device, ctx.surface_format);

  const Vertex cube_vertices[] = {
    // front (+Z), red
    {-0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f},
    {0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f},
    {0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f},
    {-0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f},

    // back (-Z), green
    {0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0.0f},
    {-0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 0.0f},
    {-0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f},
    {0.5f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f},

    // right (+X), blue
    {0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f},
    {0.5f, -0.5f, -0.5f, 0.0f, 0.0f, 1.0f},
    {0.5f, 0.5f, -0.5f, 0.0f, 0.0f, 1.0f},
    {0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 1.0f},

    // left (-X), yellow
    {-0.5f, -0.5f, -0.5f, 1.0f, 1.0f, 0.0f},
    {-0.5f, -0.5f, 0.5f, 1.0f, 1.0f, 0.0f},
    {-0.5f, 0.5f, 0.5f, 1.0f, 1.0f, 0.0f},
    {-0.5f, 0.5f, -0.5f, 1.0f, 1.0f, 0.0f},

    // top (+Y), magenta
    {-0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 1.0f},
    {0.5f, 0.5f, 0.5f, 1.0f, 0.0f, 1.0f},
    {0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 1.0f},
    {-0.5f, 0.5f, -0.5f, 1.0f, 0.0f, 1.0f},

    // bottom (-Y), cyan
    {-0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 1.0f},
    {0.5f, -0.5f, -0.5f, 0.0f, 1.0f, 1.0f},
    {0.5f, -0.5f, 0.5f, 0.0f, 1.0f, 1.0f},
    {-0.5f, -0.5f, 0.5f, 0.0f, 1.0f, 1.0f},
};

const uint16_t cube_indices[] = {
    0, 1, 2, 0, 2, 3,       // front
    4, 5, 6, 4, 6, 7,       // back
    8, 9, 10, 8, 10, 11,    // right
    12, 13, 14, 12, 14, 15, // left
    16, 17, 18, 16, 18, 19, // top
    20, 21, 22, 20, 22, 23, // bottom
};

Mesh cube_mesh =
    mesh_create(ctx.device, ctx.queue, cube_vertices,
                sizeof(cube_vertices) / sizeof(cube_vertices[0]),
                cube_indices,
                sizeof(cube_indices) / sizeof(cube_indices[0]));

  // --- Uniform buffer + bind group (new) ---
  WGPUBufferDescriptor uniform_buffer_desc = {0};
  uniform_buffer_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
  uniform_buffer_desc.size = sizeof(Mat4);
  uniform_buffer_desc.mappedAtCreation = WGPU_FALSE;

  WGPUBuffer uniform_buffer =
      wgpuDeviceCreateBuffer(ctx.device, &uniform_buffer_desc);
  if (!uniform_buffer) {
    fprintf(stderr, "Failed to create uniform buffer\n");
    exit(1);
  }

  WGPUBindGroupEntry bind_entry = {0};
  bind_entry.binding = 0;
  bind_entry.buffer = uniform_buffer;
  bind_entry.offset = 0;
  bind_entry.size = sizeof(Mat4);

  WGPUBindGroupDescriptor bind_group_desc = {0};
  bind_group_desc.layout = gfx.bind_group_layout;
  bind_group_desc.entryCount = 1;
  bind_group_desc.entries = &bind_entry;

  WGPUBindGroup bind_group =
      wgpuDeviceCreateBindGroup(ctx.device, &bind_group_desc);
  if (!bind_group) {
    fprintf(stderr, "Failed to create bind group\n");
    exit(1);
  }

  // --- Camera (new) ---
  Vec3 eye = {2.0f, 2.0f, 3.0f};
  Vec3 target = {0.0f, 0.0f, 0.0f};
  Vec3 up = {0.0f, 1.0f, 0.0f};

  Mat4 view = mat4_look_at(eye, target, up);
  Mat4 proj =
      mat4_perspective(45.0f * PERANTI_PI / 180.0f, ratio, 0.1f, 100.0f);

  while (!glfwWindowShouldClose(ctx.window)) {
    glfwPollEvents();

    // --- Per-frame model rotation + uniform upload (new) ---
    float angle = (float)glfwGetTime();
    Mat4 model = mat4_rotate_y(angle);
    Mat4 mvp = mat4_multiply(proj, mat4_multiply(view, model));
    wgpuQueueWriteBuffer(ctx.queue, uniform_buffer, 0, &mvp, sizeof(Mat4));

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

    WGPUTextureView view_texture =
        wgpuTextureCreateView(surface_texture.texture, NULL);
    WGPUCommandEncoder encoder =
        wgpuDeviceCreateCommandEncoder(ctx.device, NULL);

    WGPURenderPassColorAttachment color_attachment = {0};
    color_attachment.nextInChain = NULL;
    color_attachment.view = view_texture;
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
    wgpuRenderPassEncoderSetPipeline(pass, gfx.pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);

    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, cube_mesh.vertex_buffer, 0,
                                     cube_mesh.vertex_count * sizeof(Vertex));
wgpuRenderPassEncoderSetIndexBuffer(pass, cube_mesh.index_buffer,
                                    WGPUIndexFormat_Uint16, 0,
                                    cube_mesh.index_count * sizeof(uint16_t));
wgpuRenderPassEncoderDrawIndexed(pass, (uint32_t)cube_mesh.index_count, 1, 0, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmd_buffer_desc = {0};
    WGPUCommandBuffer cmd_buffer =
        wgpuCommandEncoderFinish(encoder, &cmd_buffer_desc);
    wgpuCommandEncoderRelease(encoder);

    wgpuQueueSubmit(ctx.queue, 1, &cmd_buffer);
    wgpuCommandBufferRelease(cmd_buffer);
    wgpuSurfacePresent(ctx.surface);
    wgpuTextureViewRelease(view_texture);
  }

  wgpuBindGroupRelease(bind_group);
  wgpuBufferRelease(uniform_buffer);
  wgpuBindGroupLayoutRelease(gfx.bind_group_layout);
  mesh_destroy(&cube_mesh);
  wgpuRenderPipelineRelease(gfx.pipeline);
  webgpu_context_destroy(&ctx);
  return 0;
}
