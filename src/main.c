#include "GLFW/glfw3.h"
#include "camera.h"
#include "mat4.h"
#include "mesh.h"
#include "pipeline.h"
#include "surface.h"
#include "text_mesh.h"
#include "text_pipeline.h"
#include "webgpu-headers/webgpu.h"
#include "webgpu_context.h"

#include <stdbool.h>
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
  WGPURenderPipeline text_gfx =
      text_pipeline_create(ctx.device, ctx.surface_format);
  Mesh fps_text_mesh = {0}; // rebuilt below once fps_text has real content
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
      0,  1,  2,  0,  2,  3,  // front
      4,  5,  6,  4,  6,  7,  // back
      8,  9,  10, 8,  10, 11, // right
      12, 13, 14, 12, 14, 15, // left
      16, 17, 18, 16, 18, 19, // top
      20, 21, 22, 20, 22, 23, // bottom
  };

  Mesh cube_mesh =
      mesh_create(ctx.device, ctx.queue, cube_vertices,
                  sizeof(cube_vertices) / sizeof(cube_vertices[0]),
                  cube_indices, sizeof(cube_indices) / sizeof(cube_indices[0]));

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
  FreeCamera camera = free_camera_init((Vec3){0.0f, 0.0f, 4.0f}, 0.0f, 0.0f);
  Mat4 proj =
      mat4_perspective(45.0f * PERANTI_PI / 180.0f, ratio, 0.1f, 100.0f);
  Mat4 model = mat4_identity(); // cube is static; camera moves instead
  float last_time = (float)glfwGetTime();

  // setup, before the while loop
  char fps_text[16] = "FPS: --"; // was static, and declared inside the loop
  TextMeshBuffer fps_text_buf =
      text_mesh_create_reserved(ctx.device, ctx.queue, sizeof(fps_text) - 1);
  while (!glfwWindowShouldClose(ctx.window)) {
    glfwPollEvents();
    float current_time = (float)glfwGetTime();
    float delta_time = current_time - last_time;
    last_time = current_time;
    static float fps_timer = 0.0f;
    static int fps_frame_count = 0;
    bool fps_text_changed = false;
    fps_timer += delta_time;
    fps_frame_count++;
    if (fps_timer >= 0.5f) {
      fps_text_changed = true;
      float fps = (float)fps_frame_count / fps_timer;
      snprintf(fps_text, sizeof(fps_text), "FPS: %d", (int)fps);
      fps_timer = 0.0f;
      fps_frame_count = 0;
    }
    if (fps_text_changed) {
      TextGeometry geo =
          text_mesh_build(fps_text, 10.0f, 10.0f, 20.0f, ctx.width, ctx.height);
      text_mesh_update(&fps_text_buf, ctx.queue, geo.vertices, geo.vertex_count,
                       geo.indices, geo.index_count);
      free(geo.vertices);
      free(geo.indices);
      fps_text_changed = false;
    }

    FreeCameraInput input = {0};
    input.move_forward = glfwGetKey(ctx.window, GLFW_KEY_W) == GLFW_PRESS;
    input.move_backward = glfwGetKey(ctx.window, GLFW_KEY_S) == GLFW_PRESS;
    input.move_up = glfwGetKey(ctx.window, GLFW_KEY_SPACE) == GLFW_PRESS;
    input.move_down =
        glfwGetKey(ctx.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(ctx.window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    input.strafe_left = glfwGetKey(ctx.window, GLFW_KEY_A) == GLFW_PRESS;
    input.strafe_right = glfwGetKey(ctx.window, GLFW_KEY_D) == GLFW_PRESS;
    input.look_left = glfwGetKey(ctx.window, GLFW_KEY_LEFT) == GLFW_PRESS;
    input.look_right = glfwGetKey(ctx.window, GLFW_KEY_RIGHT) == GLFW_PRESS;
    input.look_up = glfwGetKey(ctx.window, GLFW_KEY_UP) == GLFW_PRESS;
    input.look_down = glfwGetKey(ctx.window, GLFW_KEY_DOWN) == GLFW_PRESS;

    free_camera_update(&camera, input, delta_time);

    Mat4 view = free_camera_view_matrix(&camera);
    // --- Per-frame model rotation + uniform upload (new) ---
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
                                         cube_mesh.vertex_count *
                                             sizeof(Vertex));
    wgpuRenderPassEncoderSetIndexBuffer(
        pass, cube_mesh.index_buffer, WGPUIndexFormat_Uint16, 0,
        cube_mesh.index_count * sizeof(uint16_t));
    wgpuRenderPassEncoderDrawIndexed(pass, (uint32_t)cube_mesh.index_count, 1,
                                     0, 0, 0);
    if (fps_text_mesh.vertex_count > 0) {
      wgpuRenderPassEncoderSetPipeline(pass, text_gfx);
      wgpuRenderPassEncoderSetVertexBuffer(
          pass, 0, fps_text_mesh.vertex_buffer, 0,
          fps_text_mesh.vertex_count * sizeof(Vertex2D));
      wgpuRenderPassEncoderSetIndexBuffer(
          pass, fps_text_mesh.index_buffer, WGPUIndexFormat_Uint16, 0,
          fps_text_mesh.index_count * sizeof(uint16_t));
      wgpuRenderPassEncoderDrawIndexed(
          pass, (uint32_t)fps_text_mesh.index_count, 1, 0, 0, 0);
    }
    if (fps_text_buf.mesh.vertex_count > 0) {
      wgpuRenderPassEncoderSetPipeline(pass, text_gfx);
      wgpuRenderPassEncoderSetVertexBuffer(
          pass, 0, fps_text_buf.mesh.vertex_buffer, 0,
          fps_text_buf.mesh.vertex_count * sizeof(Vertex2D));
      wgpuRenderPassEncoderSetIndexBuffer(
          pass, fps_text_buf.mesh.index_buffer, WGPUIndexFormat_Uint16, 0,
          fps_text_buf.mesh.index_count * sizeof(uint16_t));
      wgpuRenderPassEncoderDrawIndexed(
          pass, (uint32_t)fps_text_buf.mesh.index_count, 1, 0, 0, 0);
    }

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
    wgpuTextureRelease(
        surface_texture.texture); // was missing -- the actual leak
  }
  wgpuRenderPipelineRelease(text_gfx);
  wgpuBindGroupRelease(bind_group);
  wgpuBufferRelease(uniform_buffer);
  wgpuBindGroupLayoutRelease(gfx.bind_group_layout);
  mesh_destroy(&cube_mesh);
  mesh_destroy(&fps_text_buf.mesh); // at shutdown, alongside cube_mesh
  wgpuRenderPipelineRelease(gfx.pipeline);
  webgpu_context_destroy(&ctx);
  return 0;
}
