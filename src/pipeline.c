#include "pipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Reads shaders/triangle.wgsl into a heap-allocated, null-terminated buffer.
// Caller owns the returned pointer and must free() it.
static char *read_shader_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "Failed to open shader file: %s\n", path);
    exit(1);
  }

  if (fseek(file, 0, SEEK_END) != 0) {
    fprintf(stderr, "Failed to seek shader file: %s\n", path);
    exit(1);
  }
  long file_size = ftell(file);
  if (file_size < 0) {
    fprintf(stderr, "Failed to determine shader file size: %s\n", path);
    exit(1);
  }
  rewind(file);

  char *buffer = malloc((size_t)file_size + 1);
  if (!buffer) {
    fprintf(stderr, "Failed to allocate buffer for shader file: %s\n", path);
    exit(1);
  }

  size_t bytes_read = fread(buffer, 1, (size_t)file_size, file);
  fclose(file);

  if (bytes_read != (size_t)file_size) {
    fprintf(stderr, "Failed to read full shader file: %s\n", path);
    exit(1);
  }

  buffer[file_size] = '\0';
  return buffer;
}

static WGPUShaderModule create_shader_module(WGPUDevice device,
                                             const char *wgsl_source) {
  WGPUShaderSourceWGSL wgsl_desc = {0};
  wgsl_desc.chain.sType = WGPUSType_ShaderSourceWGSL;
  wgsl_desc.code =
      (WGPUStringView){.data = wgsl_source, .length = strlen(wgsl_source)};

  WGPUShaderModuleDescriptor shader_desc = {0};
  shader_desc.nextInChain = (WGPUChainedStruct *)&wgsl_desc;

  WGPUShaderModule shader_module =
      wgpuDeviceCreateShaderModule(device, &shader_desc);
  if (!shader_module) {
    fprintf(stderr, "Failed to create WGPUShaderModule\n");
    exit(1);
  }
  return shader_module;
}

WGPURenderPipeline pipeline_create_triangle(WGPUDevice device,
                                            WGPUTextureFormat surface_format) {
  char *wgsl_source = read_shader_file("shaders/triangle.wgsl");
  WGPUShaderModule shader_module = create_shader_module(device, wgsl_source);
  free(wgsl_source);

  // --- Vertex state (no vertex buffers — positions are hardcoded in the
  // shader) ---
  WGPUVertexState vertex_state = {0};
  vertex_state.module = shader_module;
  vertex_state.entryPoint =
      (WGPUStringView){.data = "vs_main", .length = strlen("vs_main")};
  vertex_state.constantCount = 0;
  vertex_state.constants = NULL;
  vertex_state.bufferCount = 0;
  vertex_state.buffers = NULL;

  // --- Primitive state ---
  WGPUPrimitiveState primitive_state = {0};
  primitive_state.topology = WGPUPrimitiveTopology_TriangleList;
  primitive_state.stripIndexFormat = WGPUIndexFormat_Undefined;
  primitive_state.frontFace = WGPUFrontFace_CCW;
  primitive_state.cullMode = WGPUCullMode_None;

  // --- Color target + fragment state ---
  WGPUColorTargetState color_target = {0};
  color_target.format = surface_format;
  color_target.blend = NULL;
  color_target.writeMask = WGPUColorWriteMask_All;

  WGPUFragmentState fragment_state = {0};
  fragment_state.module = shader_module;
  fragment_state.entryPoint =
      (WGPUStringView){.data = "fs_main", .length = strlen("fs_main")};
  fragment_state.constantCount = 0;
  fragment_state.constants = NULL;
  fragment_state.targetCount = 1;
  fragment_state.targets = &color_target;

  // --- Pipeline ---
  WGPURenderPipelineDescriptor pipeline_desc = {0};
  pipeline_desc.layout =
      NULL; // NULL = let the implementation infer layout from the shader
  pipeline_desc.vertex = vertex_state;
  pipeline_desc.primitive = primitive_state;
  pipeline_desc.depthStencil = NULL;
  pipeline_desc.multisample =
      (WGPUMultisampleState){.nextInChain = NULL,
                             .count = 1,
                             .mask = 0xFFFFFFFF,
                             .alphaToCoverageEnabled = WGPU_FALSE};
  pipeline_desc.fragment = &fragment_state;

  WGPURenderPipeline pipeline =
      wgpuDeviceCreateRenderPipeline(device, &pipeline_desc);
  if (!pipeline) {
    fprintf(stderr, "Failed to create WGPURenderPipeline\n");
    exit(1);
  }

  wgpuShaderModuleRelease(shader_module);
  return pipeline;
}
