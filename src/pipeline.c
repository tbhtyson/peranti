#include "pipeline.h"
#include "mat4.h"
#include "mesh.h"
#include "triangle_wgsl.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Reads shaders/triangle.wgsl into a heap-allocated, null-terminated buffer.
// Caller owns the returned pointer and must free() it.

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

PipelineWithLayout pipeline_create_triangle(WGPUDevice device,
                                            WGPUTextureFormat surface_format) {
  WGPUShaderModule shader_module =
      create_shader_module(device, triangle_wgsl_source);

  // --- Vertex attribute + buffer layout ---
  WGPUVertexAttribute vertex_attrs[2] = {0};

  vertex_attrs[0].format = WGPUVertexFormat_Float32x3; // was Float32x2
  vertex_attrs[0].offset = offsetof(Vertex, x);
  vertex_attrs[0].shaderLocation = 0;

  vertex_attrs[1].format = WGPUVertexFormat_Float32x3;
  vertex_attrs[1].offset = offsetof(Vertex, r);
  vertex_attrs[1].shaderLocation = 1;

  WGPUVertexBufferLayout vertex_layout = {0};
  vertex_layout.arrayStride = sizeof(Vertex);
  vertex_layout.stepMode = WGPUVertexStepMode_Vertex;
  vertex_layout.attributeCount = 2;
  vertex_layout.attributes = vertex_attrs;

  // --- Vertex state ---
  WGPUVertexState vertex_state = {0};
  vertex_state.module = shader_module;
  vertex_state.entryPoint =
      (WGPUStringView){.data = "vs_main", .length = strlen("vs_main")};
  vertex_state.constantCount = 0;
  vertex_state.constants = NULL;
  vertex_state.bufferCount = 1;
  vertex_state.buffers = &vertex_layout;

  // --- Bind group layout: one uniform buffer, visible to the vertex stage ---
  WGPUBindGroupLayoutEntry bind_layout_entry = {0};
  bind_layout_entry.binding = 0;
  bind_layout_entry.visibility = WGPUShaderStage_Vertex;
  bind_layout_entry.buffer.type = WGPUBufferBindingType_Uniform;
  bind_layout_entry.buffer.minBindingSize = sizeof(Mat4);

  WGPUBindGroupLayoutDescriptor bind_layout_desc = {0};
  bind_layout_desc.entryCount = 1;
  bind_layout_desc.entries = &bind_layout_entry;

  WGPUBindGroupLayout bind_group_layout =
      wgpuDeviceCreateBindGroupLayout(device, &bind_layout_desc);
  if (!bind_group_layout) {
    fprintf(stderr, "Failed to create WGPUBindGroupLayout\n");
    exit(1);
  }

  // --- Pipeline layout, built from the bind group layout above ---
  WGPUPipelineLayoutDescriptor pipeline_layout_desc = {0};
  pipeline_layout_desc.bindGroupLayoutCount = 1;
  pipeline_layout_desc.bindGroupLayouts = &bind_group_layout;

  WGPUPipelineLayout pipeline_layout =
      wgpuDeviceCreatePipelineLayout(device, &pipeline_layout_desc);
  if (!pipeline_layout) {
    fprintf(stderr, "Failed to create WGPUPipelineLayout\n");
    exit(1);
  }

  // --- everything below this point is unchanged ---
  WGPUPrimitiveState primitive_state = {0};
  primitive_state.topology = WGPUPrimitiveTopology_TriangleList;
  primitive_state.stripIndexFormat = WGPUIndexFormat_Undefined;
  primitive_state.frontFace = WGPUFrontFace_CCW;
  primitive_state.cullMode = WGPUCullMode_Back;

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
  pipeline_desc.layout = pipeline_layout; // was NULL
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
  wgpuPipelineLayoutRelease(
      pipeline_layout); // baked into `pipeline` already, safe to release now

  return (PipelineWithLayout){.pipeline = pipeline,
                              .bind_group_layout = bind_group_layout};
}
